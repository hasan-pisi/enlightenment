#include "private.h"

extern Player music_player_players[];

static void
_play_state_update(E_Music_Control_Instance *inst, Eina_Bool without_delay)
{
   if (!inst->popup) return;
   if (inst->ctxt->playning)
     {
        edje_object_signal_emit(inst->content_popup, "btn,state,image,pause", "play");
        return;
     }

   if (without_delay)
     edje_object_signal_emit(inst->content_popup, "btn,state,image,play,no_delay", "play");
   else
     edje_object_signal_emit(inst->content_popup, "btn,state,image,play", "play");
}

void
music_control_state_update_all(E_Music_Control_Module_Context *ctxt)
{
   E_Music_Control_Instance *inst;
   Eina_List *list;

   EINA_LIST_FOREACH(ctxt->instances, list, inst)
     _play_state_update(inst, EINA_FALSE);
}

static void
_btn_clicked(void *data, Evas_Object *obj __UNUSED__, const char *emission __UNUSED__, const char *source)
{
   E_Music_Control_Instance *inst = data;
   if (!strcmp(source, "play"))
     media_player2_player_play_pause_call(inst->ctxt->mpris2_player);
   else if (!strcmp(source, "next"))
     media_player2_player_next_call(inst->ctxt->mpris2_player);
   else if (!strcmp(source, "previous"))
    media_player2_player_previous_call(inst->ctxt->mpris2_player);
}

static void
_label_clicked(void *data, Evas_Object *obj __UNUSED__, const char *emission __UNUSED__, const char *source __UNUSED__)
{
   E_Music_Control_Instance *inst = data;
   music_control_popup_del(inst);
   mpris_media_player2_raise_call(inst->ctxt->mrpis2);
}

static void
_player_name_update(E_Music_Control_Instance *inst)
{
   Edje_Message_String msg;
   msg.str = (char *)music_player_players[inst->ctxt->config->player_selected].name;
   edje_object_message_send(inst->content_popup, EDJE_MESSAGE_STRING, 0, &msg);
}

static void
_popup_del_cb(void *obj)
{
   music_control_popup_del(e_object_data_get(obj));
}

static void
_popup_new(E_Music_Control_Instance *inst)
{
   Evas_Object *o;
   inst->popup = e_gadcon_popup_new(inst->gcc);

   o = edje_object_add(inst->popup->win->evas);
   e_theme_edje_object_set(o, "base/theme/modules/music-control",
                           "modules/music-control/popup");
   edje_object_signal_callback_add(o, "btn,clicked", "*", _btn_clicked, inst);
   edje_object_signal_callback_add(o, "label,clicked", "player_name", _label_clicked, inst);

   e_gadcon_popup_content_set(inst->popup, o);
   inst->content_popup = o;

   _player_name_update(inst);
   _play_state_update(inst, EINA_TRUE);
   e_popup_autoclose(inst->popup->win, NULL, NULL);
   e_gadcon_popup_show(inst->popup);
   e_object_data_set(E_OBJECT(inst->popup), inst);
   E_OBJECT_DEL_SET(inst->popup, _popup_del_cb);
}

void
music_control_popup_del(E_Music_Control_Instance *inst)
{
   E_FREE_FUNC(inst->popup, e_object_del);
}

struct _E_Config_Dialog_Data
{
   int index;
};

static Evas_Object *
_cfg_widgets_create(E_Config_Dialog *cfd, Evas *evas, E_Config_Dialog_Data *cfdata)
{
   Evas_Object *o, *of, *ob;
   E_Radio_Group *rg;
   int i;
   E_Music_Control_Instance *inst = cfd->data;
   int player_selected = inst->ctxt->config->player_selected;

   o = e_widget_list_add(evas, 0, 0);

   of = e_widget_framelist_add(evas, "Music Player", 0);
   e_widget_framelist_content_align_set(of, 0.0, 0.0);
   rg = e_widget_radio_group_new(&(cfdata->index));
   for (i = 0; music_player_players[i].dbus_name; i++)
     {
        ob = e_widget_radio_add(evas, music_player_players[i].name, i, rg);
        e_widget_framelist_object_append(of, ob);
        if (i == player_selected)
          e_widget_radio_toggle_set(ob, EINA_TRUE);
     }
   ob = e_widget_label_add(evas, "* Your player must be configured to export the DBus interface MPRIS2.");
   e_widget_framelist_object_append(of, ob);
   e_widget_list_object_append(o, of, 1, 1, 0.5);

   return o;
}

static void *
_cfg_data_create(E_Config_Dialog *cfd)
{
   E_Music_Control_Instance *inst = cfd->data;
   E_Config_Dialog_Data *cfdata = calloc(1, sizeof(E_Config_Dialog_Data));
   cfdata->index = inst->ctxt->config->player_selected;
   return cfdata;
}

static void
_cfg_data_free(E_Config_Dialog *cfd __UNUSED__, E_Config_Dialog_Data *cfdata)
{
   free(cfdata);
}

static int
_cfg_check_changed(E_Config_Dialog *cfd, E_Config_Dialog_Data *cfdata)
{
   E_Music_Control_Instance *inst = cfd->data;

   return inst->ctxt->config->player_selected != cfdata->index;
}

static int
_cfg_data_apply(E_Config_Dialog *cfd, E_Config_Dialog_Data *cfdata)
{
   E_Music_Control_Instance *inst = cfd->data;
   if (inst->ctxt->config->player_selected == cfdata->index)
     return 1;

   inst->ctxt->config->player_selected = cfdata->index;
   inst->ctxt->playning = EINA_FALSE;
   mpris_media_player2_proxy_unref(inst->ctxt->mpris2_player);
   media_player2_player_proxy_unref(inst->ctxt->mrpis2);
   music_control_dbus_init(inst->ctxt,
          music_player_players[inst->ctxt->config->player_selected].dbus_name);
   return 1;
}

static void
_cb_menu_cfg(void *data, E_Menu *m, E_Menu_Item *mi __UNUSED__)
{
   E_Config_Dialog_View *v;

   v = calloc(1, sizeof(E_Config_Dialog_View));
   v->create_cfdata = _cfg_data_create;
   v->free_cfdata = _cfg_data_free;
   v->basic.create_widgets = _cfg_widgets_create;
   v->basic.apply_cfdata = _cfg_data_apply;
   v->basic.check_changed = _cfg_check_changed;

   e_config_dialog_new(m->zone->container, "Music control Settings", "E",
                       "_e_mod_music_config_dialog",
                       NULL, 0, v, data);
}

void
music_control_mouse_down_cb(void *data, Evas *evas __UNUSED__, Evas_Object *obj __UNUSED__, void *event)
{
   E_Music_Control_Instance *inst = data;
   Evas_Event_Mouse_Down *ev = event;

   if (ev->button == 1)
     {
        if (!inst->popup)
          _popup_new(inst);
        else
          music_control_popup_del(inst);
     }
   else if (ev->button == 3)
     {
        E_Menu *m;
        E_Menu_Item *mi;
        E_Zone *zone = e_util_zone_current_get(e_manager_current_get());
        int x, y;

        if (inst->popup)
          music_control_popup_del(inst);

        m = e_menu_new();
        mi = e_menu_item_new(m);
        e_menu_item_label_set(mi, "Settings");
        e_util_menu_item_theme_icon_set(mi, "configure");
        e_menu_item_callback_set(mi, _cb_menu_cfg, inst);

        m = e_gadcon_client_util_menu_items_append(inst->gcc, m, 0);

        e_gadcon_canvas_zone_geometry_get(inst->gcc->gadcon, &x, &y, NULL, NULL);
        e_menu_activate_mouse(m, zone, (x + ev->output.x),(y + ev->output.y),
                              1, 1, E_MENU_POP_DIRECTION_AUTO, ev->timestamp);
        evas_event_feed_mouse_up(inst->gcc->gadcon->evas, ev->button,
                                 EVAS_BUTTON_NONE, ev->timestamp, NULL);
     }
}
