#include "TrayIcon.hpp"

#include <dbus/dbus.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <iostream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <unistd.h>

#include "../utils/ImageLoader.hpp"
#include "../utils/Resources.hpp"

// XEmbed constants (from the freedesktop spec)
#define XEMBED_VERSION 0
#define XEMBED_MAPPED (1 << 0)
#define SYSTEM_TRAY_REQUEST_DOCK 0

// SNI introspection XML (minimal, enough for all major tray hosts)
static const char *SNI_INTROSPECT_XML = R"XML(
<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN"
  "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node name="/StatusNotifierItem">
  <interface name="org.kde.StatusNotifierItem">
    <property name="Category"     type="s" access="read"/>
    <property name="Id"           type="s" access="read"/>
    <property name="Title"        type="s" access="read"/>
    <property name="Status"       type="s" access="read"/>
    <property name="WindowId"     type="u" access="read"/>
    <property name="IconName"     type="s" access="read"/>
    <property name="IconPixmap"   type="a(iiay)" access="read"/>
    <property name="OverlayIconName"   type="s" access="read"/>
    <property name="OverlayIconPixmap" type="a(iiay)" access="read"/>
    <property name="AttentionIconName" type="s" access="read"/>
    <property name="AttentionIconPixmap" type="a(iiay)" access="read"/>
    <property name="AttentionMovieName"  type="s" access="read"/>
    <property name="ToolTip"      type="(sa(iiay)ss)" access="read"/>
    <property name="ItemIsMenu"   type="b" access="read"/>
    <property name="Menu"         type="o" access="read"/>
    <method name="Activate">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="SecondaryActivate">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="ContextMenu">
      <arg name="x" type="i" direction="in"/>
      <arg name="y" type="i" direction="in"/>
    </method>
    <method name="Scroll">
      <arg name="delta"       type="i" direction="in"/>
      <arg name="orientation" type="s" direction="in"/>
    </method>
    <signal name="NewTitle"/>
    <signal name="NewIcon"/>
    <signal name="NewAttentionIcon"/>
    <signal name="NewOverlayIcon"/>
    <signal name="NewStatus">
      <arg name="status" type="s"/>
    </signal>
  </interface>
  <interface name="org.freedesktop.DBus.Properties">
    <method name="Get">
      <arg name="interface_name" type="s" direction="in"/>
      <arg name="property_name"  type="s" direction="in"/>
      <arg name="value"          type="v" direction="out"/>
    </method>
    <method name="GetAll">
      <arg name="interface_name" type="s" direction="in"/>
      <arg name="props"          type="a{sv}" direction="out"/>
    </method>
  </interface>
  <interface name="org.freedesktop.DBus.Introspectable">
    <method name="Introspect">
      <arg name="xml_data" type="s" direction="out"/>
    </method>
  </interface>
</node>
)XML";

namespace NeonSight {

// Constructor / Destructor

TrayIcon::TrayIcon(std::function<void()> onActivate) : onActivate_(std::move(onActivate)) {}

// Enqueue an action requested from the DBus/X11 worker thread. Must be safe to
// call concurrently with PumpActions() on the main thread.
void TrayIcon::EnqueueAction(std::function<void()> action) {
  if (!action)
    return;
  std::lock_guard<std::mutex> lock(actionMutex_);
  pendingActions_.push_back(std::move(action));
}

// Drain queued actions and run them on the CALLING thread (the main loop), so
// any GLFW window operations inside them are thread-safe.
void TrayIcon::PumpActions() {
  std::deque<std::function<void()>> actions;
  {
    std::lock_guard<std::mutex> lock(actionMutex_);
    actions.swap(pendingActions_);
  }
  for (auto &action : actions) {
    action();
  }
}

TrayIcon::~TrayIcon() {
  // Shut down SNI
  if (sniRunning_) {
    sniRunning_ = false;
  }
  if (sniConn_) {
    // Private connections must be explicitly closed before the last unref
    dbus_connection_close(sniConn_);
  }
  if (sniThread_.joinable())
    sniThread_.join();
  if (sniConn_) {
    dbus_connection_unref(sniConn_);
    sniConn_ = nullptr;
  }

  // Shut down XEmbed
  x11Running_ = false;
  if (x11Display_ && x11Window_) {
    Display *dpy = static_cast<Display *>(x11Display_);
    // Send a synthetic ClientMessage with an exit atom to wake XNextEvent
    Atom exitAtom = XInternAtom(dpy, "_NEONSIGHT_EXIT", False);
    XClientMessageEvent ev{};
    ev.type = ClientMessage;
    ev.window = x11Window_;
    ev.message_type = exitAtom;
    ev.format = 32;
    XSendEvent(dpy, x11Window_, False, NoEventMask, reinterpret_cast<XEvent *>(&ev));
    XFlush(dpy);
  }
  if (x11Thread_.joinable())
    x11Thread_.join();
  if (x11Display_) {
    Display *dpy = static_cast<Display *>(x11Display_);
    if (x11Window_)
      XDestroyWindow(dpy, x11Window_);
    XCloseDisplay(dpy);
    x11Display_ = nullptr;
    x11Window_ = 0;
  }
}

// Public entry point

bool TrayIcon::Initialize() {
  // Load icon once (shared by both paths)
  int outW = 0, outH = 0;
  auto raw = ImageLoader::LoadRawFromMemory(Thumb_data, Thumb_size, outW, outH, 32);
  if (raw) {
    iconW_ = 32;
    iconH_ = 32;
    iconPixels_.resize(static_cast<size_t>(iconW_ * iconH_ * 4));
    // Convert RGBA → ARGB big-endian (SNI spec §Icons)
    for (int i = 0; i < iconW_ * iconH_; ++i) {
      iconPixels_[i * 4 + 0] = (*raw)[i * 4 + 3]; // A
      iconPixels_[i * 4 + 1] = (*raw)[i * 4 + 0]; // R
      iconPixels_[i * 4 + 2] = (*raw)[i * 4 + 1]; // G
      iconPixels_[i * 4 + 3] = (*raw)[i * 4 + 2]; // B
    }
    std::cout << "[TrayIcon] Icon loaded (" << iconW_ << "x" << iconH_ << ")\n";
  } else {
    std::cerr << "[TrayIcon] Warning: failed to load icon pixmap\n";
  }

  // Try SNI first, fall back to XEmbed
  if (InitializeSNI()) {
    std::cout << "[TrayIcon] Using SNI (StatusNotifierItem)\n";
    return true;
  }
  std::cout << "[TrayIcon] SNI unavailable, trying XEmbed...\n";
  return InitializeXEmbed();
}

//  SNI via raw libdbus-1

// Helper: append an a(iiay) variant for IconPixmap into an open message iter
static void AppendIconPixmap(DBusMessageIter *iter, const std::vector<uint8_t> &pixels, int w, int h) {
  DBusMessageIter variant, array, strct, byteArray;

  // open variant "a(iiay)"
  dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "a(iiay)", &variant);
  // open array "(iiay)"
  dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "(iiay)", &array);
  if (!pixels.empty()) {
    // open struct "iiay"
    dbus_message_iter_open_container(&array, DBUS_TYPE_STRUCT, nullptr, &strct);
    dbus_int32_t dw = static_cast<dbus_int32_t>(w);
    dbus_int32_t dh = static_cast<dbus_int32_t>(h);
    dbus_message_iter_append_basic(&strct, DBUS_TYPE_INT32, &dw);
    dbus_message_iter_append_basic(&strct, DBUS_TYPE_INT32, &dh);
    // open byte array "ay"
    dbus_message_iter_open_container(&strct, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE_AS_STRING, &byteArray);
    const uint8_t *p = pixels.data();
    int n = static_cast<int>(pixels.size());
    dbus_message_iter_append_fixed_array(&byteArray, DBUS_TYPE_BYTE, &p, n);
    dbus_message_iter_close_container(&strct, &byteArray);
    dbus_message_iter_close_container(&array, &strct);
  }
  dbus_message_iter_close_container(&variant, &array);
  dbus_message_iter_close_container(iter, &variant);
}

// Helper: append ToolTip variants
static void AppendToolTip(DBusMessageIter *iter, const std::vector<uint8_t> &pixels, int w, int h) {
  DBusMessageIter variant, strct, innerArr, innerStrct, byteArr;

  dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "(sa(iiay)ss)", &variant);
  dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, nullptr, &strct);

  const char *iconName = "";
  dbus_message_iter_append_basic(&strct, DBUS_TYPE_STRING, &iconName);

  dbus_message_iter_open_container(&strct, DBUS_TYPE_ARRAY, "(iiay)", &innerArr);
  if (!pixels.empty()) {
    dbus_message_iter_open_container(&innerArr, DBUS_TYPE_STRUCT, nullptr, &innerStrct);
    dbus_int32_t dw = static_cast<dbus_int32_t>(w);
    dbus_int32_t dh = static_cast<dbus_int32_t>(h);
    dbus_message_iter_append_basic(&innerStrct, DBUS_TYPE_INT32, &dw);
    dbus_message_iter_append_basic(&innerStrct, DBUS_TYPE_INT32, &dh);
    dbus_message_iter_open_container(&innerStrct, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE_AS_STRING, &byteArr);
    const uint8_t *p = pixels.data();
    int n = static_cast<int>(pixels.size());
    dbus_message_iter_append_fixed_array(&byteArr, DBUS_TYPE_BYTE, &p, n);
    dbus_message_iter_close_container(&innerStrct, &byteArr);
    dbus_message_iter_close_container(&innerArr, &innerStrct);
  }
  dbus_message_iter_close_container(&strct, &innerArr);

  const char *title = "NeonSight";
  const char *body = "Click to show/hide";
  dbus_message_iter_append_basic(&strct, DBUS_TYPE_STRING, &title);
  dbus_message_iter_append_basic(&strct, DBUS_TYPE_STRING, &body);

  dbus_message_iter_close_container(&variant, &strct);
  dbus_message_iter_close_container(iter, &variant);
}

// Where our com.canonical.dbusmenu lives. Declared early because the property
// handlers below advertise it via the item's "Menu" property.
static const char *kMenuPath = "/StatusNotifierItem/Menu";

// Handle a single Properties.Get or Properties.GetAll call
static DBusHandlerResult HandlePropertiesGet(DBusConnection *conn, DBusMessage *msg, const std::vector<uint8_t> &pixels, int w, int h) {
  const char *iface = nullptr;
  const char *prop = nullptr;
  if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID))
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  DBusMessage *reply = dbus_message_new_method_return(msg);
  if (!reply)
    return DBUS_HANDLER_RESULT_NEED_MEMORY;

  DBusMessageIter iter;
  dbus_message_iter_init_append(reply, &iter);

  // Helper lambdas via macro-style pattern
  auto appendString = [&](const char *val) {
    DBusMessageIter v;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &val);
    dbus_message_iter_close_container(&iter, &v);
  };
  auto appendBool = [&](dbus_bool_t val) {
    DBusMessageIter v;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "b", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_BOOLEAN, &val);
    dbus_message_iter_close_container(&iter, &v);
  };
  auto appendUint = [&](dbus_uint32_t val) {
    DBusMessageIter v;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "u", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_UINT32, &val);
    dbus_message_iter_close_container(&iter, &v);
  };
  // Object-path variant. The SNI "Menu" property is an object path ('o'), NOT a
  // plain string — hosts validate the type, so returning 's' makes them drop it.
  auto appendObjectPath = [&](const char *val) {
    DBusMessageIter v;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "o", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_OBJECT_PATH, &val);
    dbus_message_iter_close_container(&iter, &v);
  };

  std::string pname(prop ? prop : "");

  if (pname == "Category") {
    appendString("ApplicationStatus");
  } else if (pname == "Id") {
    appendString("NeonSight");
  } else if (pname == "Title") {
    appendString("NeonSight");
  } else if (pname == "Status") {
    appendString("Active");
  } else if (pname == "WindowId") {
    appendUint(0);
  } else if (pname == "IconName") {
    appendString("");
  } else if (pname == "ItemIsMenu") {
    appendBool(FALSE);
  } else if (pname == "OverlayIconName") {
    appendString("");
  } else if (pname == "AttentionIconName") {
    appendString("");
  } else if (pname == "AttentionMovieName") {
    appendString("");
  } else if (pname == "IconPixmap" || pname == "OverlayIconPixmap" || pname == "AttentionIconPixmap") {
    if (pname == "IconPixmap")
      AppendIconPixmap(&iter, pixels, w, h);
    else
      AppendIconPixmap(&iter, {}, 0, 0); // empty for overlay/attention
  } else if (pname == "ToolTip") {
    AppendToolTip(&iter, pixels, w, h);
  } else if (pname == "Menu") {
    // Path to our com.canonical.dbusmenu object. The SNI spec (and the Waybar
    // host) require this to be an OBJECT PATH, so append it as type 'o'.
    appendObjectPath(kMenuPath);
  } else {
    dbus_message_unref(reply);
    DBusMessage *err = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_PROPERTY, prop);
    dbus_connection_send(conn, err, nullptr);
    dbus_message_unref(err);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  dbus_connection_send(conn, reply, nullptr);
  dbus_message_unref(reply);
  return DBUS_HANDLER_RESULT_HANDLED;
}

// Handle Properties.GetAll
static DBusHandlerResult HandlePropertiesGetAll(DBusConnection *conn, DBusMessage *msg, const std::vector<uint8_t> &pixels, int w, int h) {
  DBusMessage *reply = dbus_message_new_method_return(msg);
  if (!reply)
    return DBUS_HANDLER_RESULT_NEED_MEMORY;

  DBusMessageIter iter, dict;
  dbus_message_iter_init_append(reply, &iter);
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);

  auto addStr = [&](const char *key, const char *val) {
    DBusMessageIter entry, v;
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &val);
    dbus_message_iter_close_container(&entry, &v);
    dbus_message_iter_close_container(&dict, &entry);
  };
  auto addBool = [&](const char *key, dbus_bool_t val) {
    DBusMessageIter entry, v;
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_BOOLEAN, &val);
    dbus_message_iter_close_container(&entry, &v);
    dbus_message_iter_close_container(&dict, &entry);
  };
  auto addUint = [&](const char *key, dbus_uint32_t val) {
    DBusMessageIter entry, v;
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "u", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_UINT32, &val);
    dbus_message_iter_close_container(&entry, &v);
    dbus_message_iter_close_container(&dict, &entry);
  };
  // "Menu" is an object path ('o') per the SNI spec.
  auto addObjPath = [&](const char *key, const char *val) {
    DBusMessageIter entry, v;
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "o", &v);
    dbus_message_iter_append_basic(&v, DBUS_TYPE_OBJECT_PATH, &val);
    dbus_message_iter_close_container(&entry, &v);
    dbus_message_iter_close_container(&dict, &entry);
  };
  auto addIconPixmap = [&](const char *key, const std::vector<uint8_t> &px, int pw, int ph) {
    DBusMessageIter entry;
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    AppendIconPixmap(&entry, px, pw, ph);
    dbus_message_iter_close_container(&dict, &entry);
  };
  auto addToolTip = [&](const char *key) {
    DBusMessageIter entry;
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    AppendToolTip(&entry, pixels, w, h);
    dbus_message_iter_close_container(&dict, &entry);
  };

  addStr("Category", "ApplicationStatus");
  addStr("Id", "NeonSight");
  addStr("Title", "NeonSight");
  addStr("Status", "Active");
  addUint("WindowId", 0);
  addStr("IconName", "");
  addBool("ItemIsMenu", FALSE);
  addStr("OverlayIconName", "");
  addStr("AttentionIconName", "");
  addStr("AttentionMovieName", "");
  addIconPixmap("IconPixmap", pixels, w, h);
  addIconPixmap("OverlayIconPixmap", {}, 0, 0);
  addIconPixmap("AttentionIconPixmap", {}, 0, 0);
  addToolTip("ToolTip");
  addObjPath("Menu", kMenuPath); // where our com.canonical.dbusmenu lives (object path)

  dbus_message_iter_close_container(&iter, &dict);
  dbus_connection_send(conn, reply, nullptr);
  dbus_message_unref(reply);
  return DBUS_HANDLER_RESULT_HANDLED;
}

//  com.canonical.dbusmenu — the right-click menu the tray host renders.
//  It lives at kMenuPath and is discovered by the host through the item's "Menu"
//  string property (see HandlePropertiesGet/GetAll). Item ids are stable and
//  advertised in GetLayout; when the user picks one, the host calls Event(id).

// The menu we expose. Keep ids stable — App.cpp maps them to actions.
struct MenuNode {
  int id;
  const char *label;
};
static const MenuNode kMenuItems[] = {
  {1, "Show/Hide Window"},
  {2, "Quit NeonSight"},
};

// Look up a menu item by id. Returns nullptr for ids we don't own.
static const MenuNode *FindMenuItem(int id) {
  for (const auto &n : kMenuItems)
    if (n.id == id)
      return &n;
  return nullptr;
}

// Append one (string) property into an open a{sv} iter.
static void MenuAddStrProp(DBusMessageIter *arr, const char *key, const char *val) {
  DBusMessageIter entry, variant;
  dbus_message_iter_open_container(arr, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
  dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &val);
  dbus_message_iter_close_container(&entry, &variant);
  dbus_message_iter_close_container(arr, &entry);
}

// Append one (boolean) property into an open a{sv} iter.
static void MenuAddBoolProp(DBusMessageIter *arr, const char *key, dbus_bool_t val) {
  DBusMessageIter entry, variant;
  dbus_message_iter_open_container(arr, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &variant);
  dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &val);
  dbus_message_iter_close_container(&entry, &variant);
  dbus_message_iter_close_container(arr, &entry);
}

// Append a single node "(ia{sv}av)" as a variant into the children array iter.
static void MenuAddChild(DBusMessageIter *childrenArr, int id, const char *label) {
  DBusMessageIter variant, node, props, children;

  dbus_message_iter_open_container(childrenArr, DBUS_TYPE_VARIANT, "(ia{sv}av)", &variant);
  dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, nullptr, &node);

  dbus_message_iter_append_basic(&node, DBUS_TYPE_INT32, &id);

  dbus_message_iter_open_container(&node, DBUS_TYPE_ARRAY, "{sv}", &props);
  MenuAddStrProp(&props, "label", label);
  MenuAddStrProp(&props, "type", "standard");
  MenuAddBoolProp(&props, "enabled", TRUE);
  MenuAddBoolProp(&props, "visible", TRUE);
  dbus_message_iter_close_container(&node, &props);

  // No sub-menu: empty children array.
  dbus_message_iter_open_container(&node, DBUS_TYPE_ARRAY, DBUS_TYPE_VARIANT_AS_STRING, &children);
  dbus_message_iter_close_container(&node, &children);

  dbus_message_iter_close_container(&variant, &node);
  dbus_message_iter_close_container(childrenArr, &variant);
}

// GetLayout(i i as) -> (u (ia{sv}av))
static DBusHandlerResult HandleDBusMenuGetLayout(DBusConnection *conn, DBusMessage *msg) {
  DBusMessage *reply = dbus_message_new_method_return(msg);
  if (!reply)
    return DBUS_HANDLER_RESULT_NEED_MEMORY;

  DBusMessageIter iter, root, rootProps, rootChildren;
  dbus_message_iter_init_append(reply, &iter);

  dbus_uint32_t revision = 0;
  dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &revision);

  // Root node: (id=0, props={children-display: submenu}, children=[ ... ])
  dbus_message_iter_open_container(&iter, DBUS_TYPE_STRUCT, nullptr, &root);
  dbus_int32_t rootId = 0;
  dbus_message_iter_append_basic(&root, DBUS_TYPE_INT32, &rootId);

  dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, "{sv}", &rootProps);
  MenuAddStrProp(&rootProps, "children-display", "submenu");
  dbus_message_iter_close_container(&root, &rootProps);

  dbus_message_iter_open_container(&root, DBUS_TYPE_ARRAY, DBUS_TYPE_VARIANT_AS_STRING, &rootChildren);
  for (const auto &item : kMenuItems)
    MenuAddChild(&rootChildren, item.id, item.label);
  dbus_message_iter_close_container(&root, &rootChildren);

  dbus_message_iter_close_container(&iter, &root);

  dbus_connection_send(conn, reply, nullptr);
  dbus_message_unref(reply);
  return DBUS_HANDLER_RESULT_HANDLED;
}

// AboutToShow(i) -> (b). We have a static menu: nothing to refresh.
static DBusHandlerResult HandleDBusMenuAboutToShow(DBusConnection *conn, DBusMessage *msg) {
  DBusMessage *reply = dbus_message_new_method_return(msg);
  if (!reply)
    return DBUS_HANDLER_RESULT_NEED_MEMORY;
  dbus_bool_t needUpdate = FALSE;
  dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &needUpdate, DBUS_TYPE_INVALID);
  dbus_connection_send(conn, reply, nullptr);
  dbus_message_unref(reply);
  return DBUS_HANDLER_RESULT_HANDLED;
}

// AboutToShowGroup(ai) -> (aib). Batch form of AboutToShow used by some hosts
// (this Waybar fork). Reply with the same parent ids, each flagged needUpdate=false.
static DBusHandlerResult HandleDBusMenuAboutToShowGroup(DBusConnection *conn, DBusMessage *msg) {
  DBusMessage *reply = dbus_message_new_method_return(msg);
  if (!reply)
    return DBUS_HANDLER_RESULT_NEED_MEMORY;

  DBusMessageIter iter, outArray, pair;
  dbus_message_iter_init_append(reply, &iter);
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "(ib)", &outArray);

  // Recurse into the single ai arg. After recurse the iterator sits ON the first
  // element (or DBUS_TYPE_INVALID if the array is empty), so process-then-advance.
  DBusMessageIter outer, inner;
  dbus_message_iter_init(msg, &outer);
  dbus_message_iter_recurse(&outer, &inner);
  while (dbus_message_iter_get_arg_type(&inner) != DBUS_TYPE_INVALID) {
    dbus_int32_t id = 0;
    dbus_message_iter_get_basic(&inner, &id);
    dbus_bool_t needUpdate = FALSE;
    dbus_message_iter_open_container(&outArray, DBUS_TYPE_STRUCT, nullptr, &pair);
    dbus_message_iter_append_basic(&pair, DBUS_TYPE_INT32, &id);
    dbus_message_iter_append_basic(&pair, DBUS_TYPE_BOOLEAN, &needUpdate);
    dbus_message_iter_close_container(&outArray, &pair);
    dbus_message_iter_next(&inner);
  }
  dbus_message_iter_close_container(&iter, &outArray);

  dbus_connection_send(conn, reply, nullptr);
  dbus_message_unref(reply);
  return DBUS_HANDLER_RESULT_HANDLED;
}

// GetGroupProperties(ai as) -> (a(ia{sv})). Return properties for the requested
// ids. Some hosts use this to (re)query item properties after GetLayout.
static DBusHandlerResult HandleDBusMenuGetGroupProperties(DBusConnection *conn, DBusMessage *msg) {
  DBusMessage *reply = dbus_message_new_method_return(msg);
  if (!reply)
    return DBUS_HANDLER_RESULT_NEED_MEMORY;

  DBusMessageIter iter, arr;
  dbus_message_iter_init_append(reply, &iter);
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "(ia{sv})", &arr);
  for (const auto &item : kMenuItems) {
    DBusMessageIter entry, props;
    dbus_message_iter_open_container(&arr, DBUS_TYPE_STRUCT, nullptr, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_INT32, &item.id);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_ARRAY, "{sv}", &props);
    MenuAddStrProp(&props, "label", item.label);
    MenuAddStrProp(&props, "type", "standard");
    MenuAddBoolProp(&props, "enabled", TRUE);
    dbus_message_iter_close_container(&entry, &props);
    dbus_message_iter_close_container(&arr, &entry);
  }
  dbus_message_iter_close_container(&iter, &arr);

  dbus_connection_send(conn, reply, nullptr);
  dbus_message_unref(reply);
  return DBUS_HANDLER_RESULT_HANDLED;
}

// GetProperty(i s) -> (v)
static DBusHandlerResult HandleDBusMenuGetProperty(DBusConnection *conn, DBusMessage *msg) {
  int32_t id = 0;
  const char *name = nullptr;
  if (!dbus_message_get_args(msg, nullptr, DBUS_TYPE_INT32, &id, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID))
    return DBUS_HANDLER_RESULT_HANDLED;

  DBusMessage *reply = dbus_message_new_method_return(msg);
  if (!reply)
    return DBUS_HANDLER_RESULT_NEED_MEMORY;

  const MenuNode *node = FindMenuItem(id);
  DBusMessageIter iter, variant;
  dbus_message_iter_init_append(reply, &iter);

  if (name && strcmp(name, "label") == 0 && node) {
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &node->label);
    dbus_message_iter_close_container(&iter, &variant);
  } else if (name && strcmp(name, "type") == 0 && node) {
    const char *t = "standard";
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &t);
    dbus_message_iter_close_container(&iter, &variant);
  } else if (name && strcmp(name, "enabled") == 0) {
    dbus_bool_t b = TRUE;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "b", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &b);
    dbus_message_iter_close_container(&iter, &variant);
  } else {
    // Unknown id/property -> empty variant (accepted by hosts).
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "", &variant);
    dbus_message_iter_close_container(&iter, &variant);
  }

  dbus_connection_send(conn, reply, nullptr);
  dbus_message_unref(reply);
  return DBUS_HANDLER_RESULT_HANDLED;
}

// GetChildren(i) -> (ai)
static DBusHandlerResult HandleDBusMenuGetChildren(DBusConnection *conn, DBusMessage *msg) {
  int32_t id = 0;
  dbus_message_get_args(msg, nullptr, DBUS_TYPE_INT32, &id, DBUS_TYPE_INVALID);

  DBusMessage *reply = dbus_message_new_method_return(msg);
  if (!reply)
    return DBUS_HANDLER_RESULT_NEED_MEMORY;

  DBusMessageIter iter, arr;
  dbus_message_iter_init_append(reply, &iter);
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, DBUS_TYPE_INT32_AS_STRING, &arr);
  if (id == 0) {
    for (const auto &item : kMenuItems)
      dbus_message_iter_append_basic(&arr, DBUS_TYPE_INT32, &item.id);
  }
  dbus_message_iter_close_container(&iter, &arr);

  dbus_connection_send(conn, reply, nullptr);
  dbus_message_unref(reply);
  return DBUS_HANDLER_RESULT_HANDLED;
}

// Properties.Get on the menu interface — hosts read Version to confirm the
// dbusmenu version (3 is the stable spec version we implement).
static DBusHandlerResult HandleMenuPropertiesGet(DBusConnection *conn, DBusMessage *msg) {
  const char *iface = nullptr;
  const char *prop = nullptr;
  dbus_message_get_args(msg, nullptr, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);

  DBusMessage *reply = dbus_message_new_method_return(msg);
  if (!reply)
    return DBUS_HANDLER_RESULT_NEED_MEMORY;

  DBusMessageIter iter, variant;
  dbus_message_iter_init_append(reply, &iter);
  if (prop && strcmp(prop, "Version") == 0) {
    dbus_uint32_t v = 3;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "u", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &v);
    dbus_message_iter_close_container(&iter, &variant);
  } else if (prop && strcmp(prop, "Status") == 0) {
    const char *s = "normal";
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &s);
    dbus_message_iter_close_container(&iter, &variant);
  } else {
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "", &variant);
    dbus_message_iter_close_container(&iter, &variant);
  }

  dbus_connection_send(conn, reply, nullptr);
  dbus_message_unref(reply);
  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult HandleMenuPropertiesGetAll(DBusConnection *conn, DBusMessage *msg) {
  DBusMessage *reply = dbus_message_new_method_return(msg);
  if (!reply)
    return DBUS_HANDLER_RESULT_NEED_MEMORY;

  DBusMessageIter iter, arr, entry, variant;
  dbus_message_iter_init_append(reply, &iter);
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &arr);
  dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
  const char *key = "Version";
  dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
  dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "u", &variant);
  dbus_uint32_t v = 3;
  dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &v);
  dbus_message_iter_close_container(&entry, &variant);
  dbus_message_iter_close_container(&arr, &entry);
  dbus_message_iter_close_container(&iter, &arr);

  dbus_connection_send(conn, reply, nullptr);
  dbus_message_unref(reply);
  return DBUS_HANDLER_RESULT_HANDLED;
}

// Event(i s v u) — a menu item was activated by the user. Dispatch to the app
// on the main thread (EnqueueAction). Only "clicked" is meaningful.
static DBusHandlerResult HandleDBusMenuEvent(DBusConnection *conn, DBusMessage *msg, void *data) {
  int32_t id = 0;
  const char *eventId = nullptr;
  dbus_message_get_args(msg, nullptr, DBUS_TYPE_INT32, &id, DBUS_TYPE_STRING, &eventId,
                        DBUS_TYPE_VARIANT, DBUS_TYPE_INVALID, DBUS_TYPE_UINT32, DBUS_TYPE_INVALID);

  auto *self = static_cast<TrayIcon *>(data);
  if (eventId && strcmp(eventId, "clicked") == 0) {
    // Capture the id; run the (main-thread) callback later via the public accessor.
    int captured = id;
    self->EnqueueAction([self, captured]() { self->InvokeMenuAction(captured); });
  }

  DBusMessage *reply = dbus_message_new_method_return(msg);
  if (reply) {
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
  }
  return DBUS_HANDLER_RESULT_HANDLED;
}

// EventGroup(a(isvu)) — the batch form of Event. Each entry is
// (id, eventId, data, timestamp). Hosts like this Waybar fork send item
// selections HERE rather than via the singular Event, so we must dispatch each
// "clicked" entry to the app (enqueued -> run on the main thread).
static DBusHandlerResult HandleDBusMenuEventGroup(DBusConnection *conn, DBusMessage *msg, void *data) {
  auto *self = static_cast<TrayIcon *>(data);

  DBusMessageIter outer, entry;
  dbus_message_iter_init(msg, &outer);
  dbus_message_iter_recurse(&outer, &entry); // entry at first (isvu) struct

  while (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_INVALID) {
    // entry is a struct; recurse to read its fields (id, eventId, ...).
    DBusMessageIter fields;
    dbus_message_iter_recurse(&entry, &fields); // fields at field 0 (id)
    int32_t id = 0;
    dbus_message_iter_get_basic(&fields, &id);
    dbus_message_iter_next(&fields); // -> field 1 (eventId string)
    const char *eventId = nullptr;
    dbus_message_iter_get_basic(&fields, &eventId);

    if (eventId && strcmp(eventId, "clicked") == 0) {
      int captured = id;
      self->EnqueueAction([self, captured]() { self->InvokeMenuAction(captured); });
    }
    dbus_message_iter_next(&entry); // -> next (isvu) struct
  }

  DBusMessage *reply = dbus_message_new_method_return(msg);
  if (reply) {
    dbus_connection_send(conn, reply, nullptr);
    dbus_message_unref(reply);
  }
  return DBUS_HANDLER_RESULT_HANDLED;
}

static const char *DBUSMENU_INTROSPECT_XML = R"XML(
<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN"
  "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node name="/StatusNotifierItem/Menu">
  <interface name="com.canonical.dbusmenu">
    <property name="Version" type="u" access="read"/>
    <method name="GetLayout">
      <arg name="parentId" type="i" direction="in"/>
      <arg name="recursionDepth" type="i" direction="in"/>
      <arg name="propertyNames" type="as" direction="in"/>
      <arg name="revision" type="u" direction="out"/>
      <arg name="layout" type="(ia{sv}av)" direction="out"/>
    </method>
    <method name="GetGroupProperties">
      <arg name="ids" type="ai" direction="in"/>
      <arg name="propertyNames" type="as" direction="in"/>
      <arg name="properties" type="a(ia{sv})" direction="out"/>
    </method>
    <method name="GetProperty">
      <arg name="id" type="i" direction="in"/>
      <arg name="name" type="s" direction="in"/>
      <arg name="value" type="v" direction="out"/>
    </method>
    <method name="GetChildren">
      <arg name="id" type="i" direction="in"/>
      <arg name="ids" type="ai" direction="out"/>
    </method>
    <method name="Event">
      <arg name="id" type="i" direction="in"/>
      <arg name="eventId" type="s" direction="in"/>
      <arg name="data" type="v" direction="in"/>
      <arg name="timestamp" type="u" direction="in"/>
    </method>
    <method name="EventGroup">
      <arg name="events" type="a(isvu)" direction="in"/>
    </method>
    <method name="AboutToShow">
      <arg name="id" type="i" direction="in"/>
      <arg name="needUpdate" type="b" direction="out"/>
    </method>
    <method name="AboutToShowGroup">
      <arg name="ids" type="ai" direction="in"/>
      <arg name="needUpdate" type="aib" direction="out"/>
    </method>
    <signal name="LayoutUpdated">
      <arg name="revision" type="u"/>
      <arg name="parent" type="i"/>
    </signal>
  </interface>
  <interface name="org.freedesktop.DBus.Properties">
    <method name="Get">
      <arg name="interface_name" type="s" direction="in"/>
      <arg name="property_name" type="s" direction="in"/>
      <arg name="value" type="v" direction="out"/>
    </method>
    <method name="GetAll">
      <arg name="interface_name" type="s" direction="in"/>
      <arg name="props" type="a{sv}" direction="out"/>
    </method>
  </interface>
  <interface name="org.freedesktop.DBus.Introspectable">
    <method name="Introspect">
      <arg name="xml_data" type="s" direction="out"/>
    </method>
  </interface>
</node>
)XML";

// libdbus message filter / dispatcher
/* static */ int TrayIcon::SNIFilterFunc(DBusConnection *conn, void *rawMsg, void *data) {
  auto *self = static_cast<TrayIcon *>(data);
  DBusMessage *msg = static_cast<DBusMessage *>(rawMsg);

  const char *path = dbus_message_get_path(msg);
  const char *iface = dbus_message_get_interface(msg);
  const char *member = dbus_message_get_member(msg);
  int type = dbus_message_get_type(msg);

  const bool isMenu = path && strcmp(path, kMenuPath) == 0;
  const bool isItem = path && strcmp(path, "/StatusNotifierItem") == 0;
  if (!isMenu && !isItem)
    return static_cast<int>(DBUS_HANDLER_RESULT_NOT_YET_HANDLED);

  // Introspect
  if (type == DBUS_MESSAGE_TYPE_METHOD_CALL && iface && strcmp(iface, "org.freedesktop.DBus.Introspectable") == 0 && member &&
      strcmp(member, "Introspect") == 0) {
    const char *xml = isMenu ? DBUSMENU_INTROSPECT_XML : SNI_INTROSPECT_XML;
    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (reply) {
      dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
      dbus_connection_send(conn, reply, nullptr);
      dbus_message_unref(reply);
    }
    return static_cast<int>(DBUS_HANDLER_RESULT_HANDLED);
  }

  // com.canonical.dbusmenu (the menu object)
  if (isMenu && type == DBUS_MESSAGE_TYPE_METHOD_CALL && iface && strcmp(iface, "com.canonical.dbusmenu") == 0 && member) {
    if (strcmp(member, "GetLayout") == 0)
      return static_cast<int>(HandleDBusMenuGetLayout(conn, msg));
    if (strcmp(member, "AboutToShow") == 0)
      return static_cast<int>(HandleDBusMenuAboutToShow(conn, msg));
    if (strcmp(member, "AboutToShowGroup") == 0)
      return static_cast<int>(HandleDBusMenuAboutToShowGroup(conn, msg));
    if (strcmp(member, "GetGroupProperties") == 0)
      return static_cast<int>(HandleDBusMenuGetGroupProperties(conn, msg));
    if (strcmp(member, "GetProperty") == 0)
      return static_cast<int>(HandleDBusMenuGetProperty(conn, msg));
    if (strcmp(member, "GetChildren") == 0)
      return static_cast<int>(HandleDBusMenuGetChildren(conn, msg));
    if (strcmp(member, "Event") == 0)
      return static_cast<int>(HandleDBusMenuEvent(conn, msg, self));
    if (strcmp(member, "EventGroup") == 0)
      return static_cast<int>(HandleDBusMenuEventGroup(conn, msg, self));
    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (reply) {
      dbus_connection_send(conn, reply, nullptr);
      dbus_message_unref(reply);
    }
    return static_cast<int>(DBUS_HANDLER_RESULT_HANDLED);
  }

  // Properties (Get/GetAll) — routed to the item or the menu object.
  if (type == DBUS_MESSAGE_TYPE_METHOD_CALL && iface && strcmp(iface, "org.freedesktop.DBus.Properties") == 0 && member) {
    if (strcmp(member, "Get") == 0)
      return isMenu ? static_cast<int>(HandleMenuPropertiesGet(conn, msg))
                    : static_cast<int>(HandlePropertiesGet(conn, msg, self->iconPixels_, self->iconW_, self->iconH_));
    if (strcmp(member, "GetAll") == 0)
      return isMenu ? static_cast<int>(HandleMenuPropertiesGetAll(conn, msg))
                    : static_cast<int>(HandlePropertiesGetAll(conn, msg, self->iconPixels_, self->iconW_, self->iconH_));
  }

  // SNI Methods (item object only)
  if (isItem && type == DBUS_MESSAGE_TYPE_METHOD_CALL && iface &&
      (strcmp(iface, "org.kde.StatusNotifierItem") == 0 || strcmp(iface, "org.freedesktop.StatusNotifierItem") == 0)) {

    // Left-click: toggle the window. This arrives on the worker thread, but the
    // callback touches GLFW (only safe on the main thread), so we enqueue it —
    // the main loop runs it via PumpActions().
    if (member && strcmp(member, "Activate") == 0) {
      self->EnqueueAction(self->onActivate_);
    }
    // ContextMenu / SecondaryActivate / Scroll: the tray host renders our
    // com.canonical.dbusmenu itself (it finds it via the "Menu" property); item
    // selections come back through the menu's Event() method. We just ack these.
    DBusMessage *reply = dbus_message_new_method_return(msg);
    if (reply) {
      dbus_connection_send(conn, reply, nullptr);
      dbus_message_unref(reply);
    }
    return static_cast<int>(DBUS_HANDLER_RESULT_HANDLED);
  }

  return static_cast<int>(DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
}

// Attempt to register with SNI watcher(s)
static bool TryRegisterWithWatcher(DBusConnection *conn, const std::string &serviceName, const char *watcherService) {
  DBusError err;
  dbus_error_init(&err);

  DBusMessage *msg = dbus_message_new_method_call(watcherService, "/StatusNotifierWatcher", watcherService, "RegisterStatusNotifierItem");
  if (!msg)
    return false;

  const char *svcStr = serviceName.c_str();
  dbus_message_append_args(msg, DBUS_TYPE_STRING, &svcStr, DBUS_TYPE_INVALID);

  DBusPendingCall *pending = nullptr;
  bool sent = dbus_connection_send_with_reply(conn, msg, &pending, 2000);
  dbus_message_unref(msg);

  if (!sent || !pending)
    return false;

  dbus_pending_call_block(pending);
  DBusMessage *reply = dbus_pending_call_steal_reply(pending);
  dbus_pending_call_unref(pending);

  bool ok = false;
  if (reply) {
    ok = (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN);
    dbus_message_unref(reply);
  }
  return ok;
}

// TrayIcon::InitializeSNI
bool TrayIcon::InitializeSNI() {
  DBusError err;
  dbus_error_init(&err);

  DBusConnection *conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
  if (!conn || dbus_error_is_set(&err)) {
    std::cerr << "[TrayIcon] SNI: cannot connect to session bus: " << (dbus_error_is_set(&err) ? err.message : "unknown") << "\n";
    dbus_error_free(&err);
    return false;
  }
  // Don't let libdbus terminate us on disconnect
  dbus_connection_set_exit_on_disconnect(conn, FALSE);

  // Build a unique service name
  sniServiceName_ = "org.kde.StatusNotifierItem-NeonSight-" + std::to_string(static_cast<long>(getpid()));

  int ret = dbus_bus_request_name(conn, sniServiceName_.c_str(), DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
  if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
    std::cerr << "[TrayIcon] SNI: could not own service name '" << sniServiceName_ << "': " << (dbus_error_is_set(&err) ? err.message : "unknown")
              << "\n";
    dbus_error_free(&err);
    dbus_connection_close(conn);
    dbus_connection_unref(conn);
    return false;
  }

  // Install our message filter
  dbus_connection_add_filter(conn, reinterpret_cast<DBusHandleMessageFunction>(SNIFilterFunc), this, nullptr);

  // Try to register with whichever watcher is available
  bool registered = TryRegisterWithWatcher(conn, sniServiceName_, "org.kde.StatusNotifierWatcher") ||
                    TryRegisterWithWatcher(conn, sniServiceName_, "org.freedesktop.StatusNotifierWatcher");

  if (!registered) {
    std::cerr << "[TrayIcon] SNI: no StatusNotifierWatcher found on session bus\n";
    dbus_connection_close(conn);
    dbus_connection_unref(conn);
    return false;
  }
  std::cout << "[TrayIcon] SNI: registered with watcher as '" << sniServiceName_ << "'\n";

  // Emit NewIcon so the host reads our icon immediately
  DBusMessage *sig = dbus_message_new_signal("/StatusNotifierItem", "org.kde.StatusNotifierItem", "NewIcon");
  if (sig) {
    dbus_connection_send(conn, sig, nullptr);
    dbus_message_unref(sig);
  }
  dbus_connection_flush(conn);

  sniConn_ = conn;
  sniRunning_ = true;
  sniThread_ = std::thread(&TrayIcon::SNILoop, this);
  return true;
}

// TrayIcon::SNILoop — dispatch loop
void TrayIcon::SNILoop() {
  while (sniRunning_ && sniConn_) {
    if (!dbus_connection_read_write_dispatch(sniConn_, 50 /*ms*/)) {
      // Connection was closed
      break;
    }
  }
  std::cout << "[TrayIcon] SNI dispatch loop exited\n";
}

//  XEmbed (_NET_SYSTEM_TRAY_S0) fallback

bool TrayIcon::InitializeXEmbed() {
  Display *dpy = XOpenDisplay(nullptr);
  if (!dpy) {
    std::cerr << "[TrayIcon] XEmbed: cannot open display\n";
    return false;
  }
  x11Display_ = dpy;

  int screen = DefaultScreen(dpy);
  Window root = RootWindow(dpy, screen);

  // Build tray selection atom name
  char selName[64];
  std::snprintf(selName, sizeof(selName), "_NET_SYSTEM_TRAY_S%d", screen);
  Atom selAtom = XInternAtom(dpy, selName, False);

  // Poll up to 10 seconds for the tray manager to appear
  Window trayOwner = None;
  for (int i = 0; i < 20; ++i) {
    trayOwner = XGetSelectionOwner(dpy, selAtom);
    if (trayOwner != None)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "[TrayIcon] XEmbed: waiting for tray manager... (" << (i + 1) * 500 << "ms)\n";
  }

  if (trayOwner == None) {
    std::cerr << "[TrayIcon] XEmbed: no tray manager found after 10s\n";
    XCloseDisplay(dpy);
    x11Display_ = nullptr;
    return false;
  }
  std::cout << "[TrayIcon] XEmbed: tray manager found (window 0x" << std::hex << trayOwner << std::dec << ")\n";

  // Create the icon window
  // Match depth 24 (safe default), polybar won't care about alpha here
  x11Window_ = XCreateSimpleWindow(dpy, root, 0, 0, 24, 24, 0, BlackPixel(dpy, screen), BlackPixel(dpy, screen));

  XSelectInput(dpy, x11Window_, ExposureMask | ButtonPressMask | StructureNotifyMask);

  // Set _XEMBED_INFO property (required by XEmbed spec)
  Atom xembedInfo = XInternAtom(dpy, "_XEMBED_INFO", False);
  unsigned long xembedData[2] = {XEMBED_VERSION, XEMBED_MAPPED};
  XChangeProperty(dpy, x11Window_, xembedInfo, xembedInfo, 32, PropModeReplace, reinterpret_cast<unsigned char *>(xembedData), 2);

  // Send SYSTEM_TRAY_REQUEST_DOCK opcode
  Atom opcodeAtom = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
  XClientMessageEvent ev{};
  ev.type = ClientMessage;
  ev.window = trayOwner;
  ev.message_type = opcodeAtom;
  ev.format = 32;
  ev.data.l[0] = CurrentTime;
  ev.data.l[1] = SYSTEM_TRAY_REQUEST_DOCK;
  ev.data.l[2] = static_cast<long>(x11Window_);
  XSendEvent(dpy, trayOwner, False, NoEventMask, reinterpret_cast<XEvent *>(&ev));
  XSync(dpy, False);

  x11Running_ = true;
  x11Thread_ = std::thread(&TrayIcon::XEmbedLoop, this);
  return true;
}

// TrayIcon::XEmbedLoop — event loop for XEmbed window
void TrayIcon::XEmbedLoop() {
  Display *dpy = static_cast<Display *>(x11Display_);
  int screen = DefaultScreen(dpy);
  Atom exitAtom = XInternAtom(dpy, "_NEONSIGHT_EXIT", False);

  // Helper: build a fresh XImage scaled to (w, h) from iconPixels_
  // Caller is responsible for XDestroyImage when done (which also frees buf).
  auto buildImage = [&](int w, int h) -> XImage * {
    auto *buf = static_cast<uint32_t *>(std::malloc(static_cast<size_t>(w * h) * 4));
    if (!buf)
      return nullptr;

    if (!iconPixels_.empty()) {
      // Nearest-neighbour scale from (iconW_ x iconH_) ARGB → native BGRX
      for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
          int srcX = px * iconW_ / w;
          int srcY = py * iconH_ / h;
          int si = (srcY * iconW_ + srcX) * 4;
          uint8_t r = iconPixels_[si + 1];
          uint8_t g = iconPixels_[si + 2];
          uint8_t b = iconPixels_[si + 3];
          buf[py * w + px] = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
        }
      }
    } else {
      for (int i = 0; i < w * h; ++i)
        buf[i] = 0xFF00FFu; // magenta placeholder
    }

    XImage *img = XCreateImage(dpy, DefaultVisual(dpy, screen), static_cast<unsigned>(DefaultDepth(dpy, screen)), ZPixmap, 0,
                               reinterpret_cast<char *>(buf), static_cast<unsigned>(w), static_cast<unsigned>(h), 32, 0);
    if (!img)
      std::free(buf);
    return img;
  };

  // Query the actual window geometry right now (polybar may have already
  // reparented and resized us before the event loop starts)
  Window root_ret;
  int x_ret, y_ret;
  unsigned int winW = 24, winH = 24, bord, depth;
  XGetGeometry(dpy, x11Window_, &root_ret, &x_ret, &y_ret, &winW, &winH, &bord, &depth);

  XImage *ximg = buildImage(static_cast<int>(winW), static_cast<int>(winH));

  while (x11Running_) {
    XEvent ev;
    XNextEvent(dpy, &ev);

    if (ev.type == ClientMessage && ev.xclient.message_type == exitAtom) {
      break;
    }

    // Polybar resizes the embedded window — rebuild the image at new size
    if (ev.type == ConfigureNotify) {
      unsigned nW = static_cast<unsigned>(ev.xconfigure.width);
      unsigned nH = static_cast<unsigned>(ev.xconfigure.height);
      if (nW != winW || nH != winH) {
        winW = nW;
        winH = nH;
        if (ximg)
          XDestroyImage(ximg);
        ximg = buildImage(static_cast<int>(winW), static_cast<int>(winH));
      }
      // Force a full repaint at new size
      XClearArea(dpy, x11Window_, 0, 0, 0, 0, True);
    }

    if (ev.type == Expose && ev.xexpose.count == 0) {
      if (ximg) {
        XPutImage(dpy, x11Window_, DefaultGC(dpy, screen), ximg, 0, 0, 0, 0, winW, winH);
      }
    }

    // Left-click (Button1) -> activate. Enqueue rather than call directly: this
    // runs on the X11 worker thread and the callback touches GLFW (main thread).
    // Right-click has no dbusmenu equivalent on a plain X11 tray — leave as no-op.
    if (ev.type == ButtonPress && ev.xbutton.button == Button1) {
      EnqueueAction(onActivate_);
    }
  }

  if (ximg)
    XDestroyImage(ximg);
  std::cout << "[TrayIcon] XEmbed loop exited\n";
}

} // namespace NeonSight
