#include "e.h"

typedef struct _E_Widget_Data E_Widget_Data;
struct _E_Widget_Data
{
   Evas_Object *obj, *table;
   Eina_List   *desks;
   int          w, h, dx, dy, cx, cy;
};
typedef struct _E_Widget_Desk_Data E_Widget_Desk_Data;
struct _E_Widget_Desk_Data
{
   Evas_Object         *icon, *live, *cont;
   int                  zone, x, y;
   Ecore_Event_Handler *bg_upd_hdl;
   Ecore_Job           *resize_job;
   Eina_Bool            configurable : 1;
};

/* local function prototypes */
static void      _e_wid_data_del(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED);
static void      _e_wid_del_hook(Evas_Object *obj);
static void      _e_wid_reconfigure(E_Widget_Data *wd);
static void      _e_wid_desk_cb_config(void *data, Evas *evas, Evas_Object *obj, void *event);
static void      _e_wid_cb_resize(void *data, Evas *evas, Evas_Object *obj, void *event);
static Eina_Bool _e_wid_cb_bg_update(void *data, int type, void *event);

static void
_bgpreview_viewport_update(Evas_Object *o, const E_Zone *zone, int x, int y)
{
   Edje_Message_Float_Set *msg;

   msg = alloca(sizeof(Edje_Message_Float_Set) + (4 * sizeof(double)));
   msg->count = 5;
   msg->val[0] = 0.2 * (!!e_config->desk_flip_animate_mode);//e_config->desk_flip_animate_time;
   msg->val[1] = x;
   msg->val[2] = zone->desk_x_count;
   msg->val[3] = y;
   msg->val[4] = zone->desk_y_count;
   edje_object_message_send(o, EDJE_MESSAGE_FLOAT_SET, 0, msg);
}

E_API Evas_Object *
e_widget_bgpreview_add(Evas *evas, int nx, int ny)
{
   Evas_Object *obj;
   E_Widget_Data *wd;

   obj = e_widget_add(evas);
   wd = E_NEW(E_Widget_Data, 1);
   wd->obj = obj;
   wd->dx = nx;
   wd->dy = ny;
   e_widget_data_set(obj, wd);
   e_widget_del_hook_set(obj, _e_wid_del_hook);

   wd->table = evas_object_table_add(evas);
   evas_object_table_padding_set(wd->table, 0, 0);
   evas_object_table_align_set(wd->table, 0.5, 0.5);
   e_widget_resize_object_set(wd->obj, wd->table);
   evas_object_show(wd->table);
   e_widget_sub_object_add(wd->obj, wd->table);

   e_widget_bgpreview_num_desks_set(obj, wd->dx, wd->dy);

   evas_object_event_callback_add(obj, EVAS_CALLBACK_RESIZE,
                                  _e_wid_cb_resize, NULL);
   return obj;
}

E_API void
e_widget_bgpreview_num_desks_set(Evas_Object *obj, int nx, int ny)
{
   E_Widget_Data *wd;

   if (!(wd = e_widget_data_get(obj))) return;
   wd->dx = nx;
   wd->dy = ny;
   _e_wid_reconfigure(wd);
}

E_API Evas_Object *
e_widget_bgpreview_desk_add(Evas *e, E_Zone *zone, int x, int y)
{
   E_Widget_Desk_Data *dd;
   Evas_Object *o;
   const char *bgfile;

   bgfile = e_bg_file_get(zone->num, x, y);

   dd = E_NEW(E_Widget_Desk_Data, 1);
   dd->zone = zone->num;
   dd->x = x;
   dd->y = y;

   dd->cont = evas_object_table_add(e);

   if (eina_str_has_extension(bgfile, ".edj"))
     {
        dd->live = o = edje_object_add(e);
        edje_object_file_set(o, bgfile, "e/desktop/background");
        _bgpreview_viewport_update(o, zone, x, y);
     }
   else if ((eina_str_has_extension(bgfile, ".gif")) ||
            (eina_str_has_extension(bgfile, ".png")) ||
            (eina_str_has_extension(bgfile, ".jpg")) ||
            (eina_str_has_extension(bgfile, ".jpeg")) ||
            (eina_str_has_extension(bgfile, ".bmp")))
     {
        dd->live = o = e_icon_add(e);
        e_icon_file_key_set(o, bgfile, NULL);
        e_icon_fill_inside_set(o, 0);
     }
   else
     {
        dd->live = o = e_video_add(e, bgfile, EINA_TRUE);
     }
   eina_stringshare_del(bgfile);

   evas_object_size_hint_weight_set(dd->live, 1, 1);
   evas_object_size_hint_align_set(dd->live, -1, -1);
   evas_object_table_pack(dd->cont, dd->live, 0, 0, 1, 1);
   evas_object_show(dd->live);

   evas_object_data_set(dd->cont, "desk_data", dd);
   evas_object_event_callback_add(dd->cont, EVAS_CALLBACK_DEL, _e_wid_data_del, dd);

   dd->bg_upd_hdl = ecore_event_handler_add(E_EVENT_BG_UPDATE,
                                            _e_wid_cb_bg_update, dd);

   return dd->cont;
}

E_API void
e_widget_bgpreview_desk_configurable_set(Evas_Object *obj, Eina_Bool enable)
{
   E_Widget_Desk_Data *dd;

   EINA_SAFETY_ON_NULL_RETURN(obj);
   dd = evas_object_data_get(obj, "desk_data");
   EINA_SAFETY_ON_NULL_RETURN(dd);
   enable = !!enable;
   if (dd->configurable == enable) return;
   if (enable)
     evas_object_event_callback_add(dd->icon, EVAS_CALLBACK_MOUSE_UP,
                                    _e_wid_desk_cb_config, dd);
   else
     evas_object_event_callback_del_full(dd->icon, EVAS_CALLBACK_MOUSE_UP,
                                         _e_wid_desk_cb_config, dd);
   dd->configurable = enable;
}

static void
_e_wid_data_del(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Widget_Desk_Data *dd;
   dd = data;
   if (!dd) return;
   if (dd->bg_upd_hdl) ecore_event_handler_del(dd->bg_upd_hdl);
   evas_object_del(dd->live);
   ecore_job_del(dd->resize_job);
   free(dd);
}

static void
_e_wid_del_hook(Evas_Object *obj)
{
   E_Widget_Data *wd;
   Evas_Object *o;

   if (!(wd = e_widget_data_get(obj))) return;

   EINA_LIST_FREE(wd->desks, o)
     evas_object_del(o);

   E_FREE(wd);
}

static void
_e_wid_reconfigure(E_Widget_Data *wd)
{
   Eina_List *l, *ll;
   Evas_Object *dw;
   E_Zone *zone;
   int tw, th, mw, mh, y;
   E_Widget_Desk_Data *dd;
   double zone_ratio, desk_ratio;

   zone = e_zone_current_get();

   evas_object_geometry_get(wd->table, NULL, NULL, &tw, &th);
   if ((tw == 0) || (th == 0))
     {
        mw = mh = 0;
     }
   else
     {
        zone_ratio = (double)zone->w / zone->h;
        desk_ratio = (tw / wd->dx) / (th / wd->dy);

        if (zone_ratio > desk_ratio)
          {
             mw = tw / wd->dx;
             mh = mw / zone_ratio;
          }
        else
          {
             mh = th / wd->dy;
             mw = mh * zone_ratio;
          }
     }

   EINA_LIST_FOREACH_SAFE(wd->desks, l, ll, dw)
     {
        if (!(dd = evas_object_data_get(dw, "desk_data"))) continue;
        if ((dd->x < wd->dx) && (dd->y < wd->dy))
          {
             evas_object_size_hint_min_set(dw, mw, mh);
             evas_object_size_hint_max_set(dw, mw, mh);
          }
        else
          {
             evas_object_del(dd->live);
             evas_object_del(dw);
             wd->desks = eina_list_remove(wd->desks, dw);
          }
     }

   for (y = 0; y < wd->dy; y++)
     {
        int x, sx;

        if (y >= wd->cy) sx = 0;
        else sx = wd->cx;
        for (x = sx; x < wd->dx; x++)
          {
             Evas_Object *dp;
             Evas *e;

             e = evas_object_evas_get(wd->obj);
             dp = e_widget_bgpreview_desk_add(e, zone, x, y);

             dd = evas_object_data_get(dp, "desk_data");
             dp = dd->icon = edje_object_add(e);
             e_theme_edje_object_set(dd->icon, "base/theme/widgets",
                                     "e/widgets/deskpreview/desk");

             edje_object_part_swallow(dd->icon, "e.swallow.content", dd->cont);
             dd->configurable = EINA_TRUE;
             evas_object_event_callback_add(dd->icon, EVAS_CALLBACK_MOUSE_UP,
                                            _e_wid_desk_cb_config, dd);
             evas_object_show(dd->icon);
             evas_object_data_set(dd->icon, "desk_data", dd);
             evas_object_size_hint_min_set(dp, mw, mh);
             evas_object_size_hint_max_set(dp, mw, mh);
             evas_object_size_hint_aspect_set(dp, EVAS_ASPECT_CONTROL_BOTH, zone->w, zone->h);
             evas_object_table_pack(wd->table, dp, x, y, 1, 1);
             wd->desks = eina_list_append(wd->desks, dp);
          }
     }

   wd->cx = wd->dx;
   wd->cy = wd->dy;
}

static void
_e_wid_desk_cb_config(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Widget_Desk_Data *dd;
   Evas_Event_Mouse_Down *ev;

   ev = event;
   if (!(dd = data)) return;
   if (ev->button == 1)
     {
        char buff[256];

        snprintf(buff, sizeof(buff), "%i %i %i",
                 dd->zone, dd->x, dd->y);
        e_configure_registry_call("internal/desk", NULL, buff);
     }
}

static void
_e_wid_cb_resize(void *data EINA_UNUSED, Evas *evas EINA_UNUSED, Evas_Object *obj, void *event EINA_UNUSED)
{
   E_Widget_Data *wd;

   if (!(wd = e_widget_data_get(obj))) return;
   _e_wid_reconfigure(wd);
}

static Eina_Bool
_e_wid_cb_bg_update(void *data, int type, void *event)
{
   E_Event_Bg_Update *ev;
   E_Widget_Desk_Data *dd;

   if (type != E_EVENT_BG_UPDATE) return ECORE_CALLBACK_PASS_ON;
   if (!(dd = data)) return ECORE_CALLBACK_PASS_ON;
   ev = event;

   if (((ev->zone < 0) || (dd->zone == ev->zone)) &&
       ((ev->desk_x < 0) || (dd->x == ev->desk_x)) &&
       ((ev->desk_y < 0) || (dd->y == ev->desk_y)))
     {
        E_Zone *zone;
        const char *bgfile;
        Evas_Object *o;
        Evas *e = evas_object_evas_get(dd->cont);

        zone = e_comp_zone_number_get(dd->zone);
        bgfile = e_bg_file_get(dd->zone, dd->x, dd->y);

        if (dd->live) evas_object_del(dd->live);
        dd->live = NULL;

        if (eina_str_has_extension(bgfile, ".edj"))
          {
             dd->live = o = edje_object_add(e);
             edje_object_file_set(o, bgfile, "e/desktop/background");
             _bgpreview_viewport_update(o, zone, dd->x, dd->y);
          }
        else if ((eina_str_has_extension(bgfile, ".gif")) ||
                 (eina_str_has_extension(bgfile, ".png")) ||
                 (eina_str_has_extension(bgfile, ".jpg")) ||
                 (eina_str_has_extension(bgfile, ".jpeg")) ||
                 (eina_str_has_extension(bgfile, ".bmp")))
          {
             dd->live = o = e_icon_add(e);
             e_icon_file_key_set(o, bgfile, NULL);
             e_icon_fill_inside_set(o, 0);
          }
        else
          {
             dd->live = o = e_video_add(e, bgfile, EINA_TRUE);
          }
        eina_stringshare_del(bgfile);

        evas_object_size_hint_weight_set(dd->live, 1, 1);
        evas_object_size_hint_align_set(dd->live, -1, -1);
        evas_object_table_pack(dd->cont, dd->live, 0, 0, 1, 1);
        evas_object_show(dd->live);
     }

   return ECORE_CALLBACK_PASS_ON;
}

