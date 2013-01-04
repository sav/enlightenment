#include "e_mod_appmenu_private.h"

#define REGISTRAR_BUS "com.canonical.AppMenu.Registrar"
#define REGISTRAR_PATH "/com/canonical/AppMenu/Registrar"
#define REGISTRAR_IFACE "com.canonical.AppMenu.Registrar"

#define ERROR_WINDOW_NOT_FOUND "com.canonical.AppMenu.Registrar.WindowNotFound"

enum
{
   SIGNAL_WINDOW_REGISTERED = 0,
   SIGNAL_WINDOW_UNREGISTERED,
};

void
appmenu_application_monitor(void *data, const char *bus EINA_UNUSED, const char *old __UNUSED__, const char *new __UNUSED__)
{
   E_AppMenu_Window *window = data;
   edbus_service_signal_emit(window->ctxt->iface, SIGNAL_WINDOW_UNREGISTERED,
                             window->window_id);
   appmenu_window_free(window);
}

static void
menu_update_cb(void *data, E_DBusMenu_Item *item)
{
   E_AppMenu_Window *window = data;
   window->root_item = item;
   if (!item)
     return;
   if (window->window_id == window->ctxt->window_with_focus)
     appmenu_menu_render(window->ctxt, window);
}

static void
menu_pop_cb(void *data EINA_UNUSED, const E_DBusMenu_Item *new_root_item EINA_UNUSED)
{
   //TODO
}

static EDBus_Message *
_on_register_window(const EDBus_Service_Interface *iface, const EDBus_Message *msg)
{
   EDBus_Connection *conn = edbus_service_connection_get(iface);
   E_AppMenu_Context *ctxt = edbus_service_object_data_get(iface, "ctxt");
   unsigned window_id;
   const char *path, *bus_id;
   E_AppMenu_Window *window;

   if (!edbus_message_arguments_get(msg, "uo", &window_id, &path))
     {
        ERR("Error reading message");
        return NULL;
     }

   window = calloc(1, sizeof(E_AppMenu_Window));
   EINA_SAFETY_ON_NULL_RETURN_VAL(window, NULL);


   bus_id = edbus_message_sender_get(msg);

   window->window_id = window_id;
   window->dbus_menu = e_dbusmenu_load(conn, bus_id, path, window);
   e_dbusmenu_update_cb_set(window->dbus_menu, menu_update_cb);
   e_dbusmenu_pop_request_cb_set(window->dbus_menu, menu_pop_cb);
   window->bus_id = eina_stringshare_add(bus_id);
   window->path = eina_stringshare_add(path);

   edbus_name_owner_changed_callback_add(conn, bus_id, appmenu_application_monitor,
                                         window, EINA_FALSE);
   ctxt->windows = eina_list_append(ctxt->windows, window);
   window->ctxt = ctxt;

   edbus_service_signal_emit(iface, SIGNAL_WINDOW_REGISTERED, window_id,
                             bus_id, path);
   return edbus_message_method_return_new(msg);
}

static E_AppMenu_Window *
window_find(E_AppMenu_Context *ctxt, unsigned window_id)
{
   Eina_List *l;
   E_AppMenu_Window *w;
   EINA_LIST_FOREACH(ctxt->windows, l, w)
     {
        if (w->window_id == window_id)
          return w;
     }
   return NULL;
}

static EDBus_Message *
_on_unregister_window(const EDBus_Service_Interface *iface, const EDBus_Message *msg)
{
   E_AppMenu_Context *ctxt = edbus_service_object_data_get(iface, "ctxt");
   E_AppMenu_Window *w;
   unsigned window_id;

   if (!edbus_message_arguments_get(msg, "u", &window_id))
     {
        ERR("Error reading message.");
        return NULL;
     }

   w = window_find(ctxt, window_id);
   if (w)
     appmenu_window_free(w);
   edbus_service_signal_emit(iface, SIGNAL_WINDOW_UNREGISTERED, window_id);
   return edbus_message_method_return_new(msg);
}

static EDBus_Message *
_on_getmenu(const EDBus_Service_Interface *iface, const EDBus_Message *msg)
{
   unsigned window_id;
   Eina_List *l;
   E_AppMenu_Window *w;
   E_AppMenu_Context *ctxt = edbus_service_object_data_get(iface, "ctxt");

   if (!edbus_message_arguments_get(msg, "u", &window_id))
     {
        ERR("Error reading message");
        return NULL;
     }
   EINA_LIST_FOREACH(ctxt->windows, l, w)
     {
        if (w->window_id == window_id)
          {
             EDBus_Message *reply;
             reply = edbus_message_method_return_new(msg);
             edbus_message_arguments_append(reply, "so", w->bus_id, w->path);
             return reply;
          }
     }
   return edbus_message_error_new(msg, ERROR_WINDOW_NOT_FOUND, "");
}

static EDBus_Message *
_on_getmenus(const EDBus_Service_Interface *iface, const EDBus_Message *msg)
{
   Eina_List *l;
   E_AppMenu_Window *w;
   E_AppMenu_Context *ctxt = edbus_service_object_data_get(iface, "ctxt");
   EDBus_Message *reply;
   EDBus_Message_Iter *array, *main_iter;

   reply = edbus_message_method_return_new(msg);
   main_iter = edbus_message_iter_get(reply);
   edbus_message_iter_arguments_append(main_iter, "a(uso)", &array);

   EINA_LIST_FOREACH(ctxt->windows, l, w)
     {
        EDBus_Message_Iter *entry;
        edbus_message_iter_arguments_append(array, "(uso)", &entry);
        edbus_message_iter_arguments_append(entry, "uso", w->window_id,
                                            w->bus_id, w->path);
        edbus_message_iter_container_close(array, entry);
     }

   edbus_message_iter_container_close(main_iter, array);
   return reply;
}

static const EDBus_Method registrar_methods[] = {
   { "RegisterWindow", EDBUS_ARGS({"u", "windowId"},{"o", "menuObjectPath"}),
      NULL, _on_register_window },
   { "UnregisterWindow", EDBUS_ARGS({"u", "windowId"}),
      NULL, _on_unregister_window },
   { "GetMenuForWindow", EDBUS_ARGS({"u", "windowId"}),
     EDBUS_ARGS({"s", "bus_id"},{"o", "menu_path"}), _on_getmenu },
   { "GetMenus", NULL, EDBUS_ARGS({"a(uso)", "array_of_menu"}), _on_getmenus },
   { }
};

static const EDBus_Signal registrar_signals[] = {
   [SIGNAL_WINDOW_REGISTERED] =
     { "WindowRegistered",
        EDBUS_ARGS({"u", "windowId"}, {"s", "bus_id"}, {"o", "menu_path"}) },
   [SIGNAL_WINDOW_UNREGISTERED] =
     { "WindowUnregistered", EDBUS_ARGS({"u", "windowId"}) },
   { }
};

static const EDBus_Service_Interface_Desc registrar_iface = {
   REGISTRAR_IFACE, registrar_methods, registrar_signals
};

void
appmenu_dbus_registrar_server_init(E_AppMenu_Context *ctx)
{
   ctx->iface = edbus_service_interface_register(ctx->conn,
                                                 REGISTRAR_PATH,
                                                 &registrar_iface);
   edbus_service_object_data_set(ctx->iface, "ctxt", ctx);
   edbus_name_request(ctx->conn, REGISTRAR_BUS,
                      EDBUS_NAME_REQUEST_FLAG_REPLACE_EXISTING, NULL, NULL);
}
