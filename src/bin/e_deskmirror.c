#include "e.h"

#define SMART_NAME "e_deskmirror"

#define INTERNAL_ENTRY E_Smart_Data *sd; sd = evas_object_smart_data_get(obj); if (!sd) return;

#define API_ENTRY(X)   E_Smart_Data *sd; \
                       sd = evas_object_smart_data_get(X); \
                       if ((!X) || (!sd) || (e_util_strcmp(evas_object_type_get(X), SMART_NAME))) return

typedef struct E_Smart_Data
{
   Evas *e;
   Evas_Object *obj;
   Evas_Object *clip;
   Evas_Object *bgpreview;
   Evas_Object *layout;
   Eina_Inlist *mirrors;
   Eina_Hash *mirror_hash;

   Eina_List *handlers;

   Evas_Coord x, y;
   int w, h;

   E_Desk *desk;
   E_Object_Delfn *desk_delfn;

   Eina_Bool pager : 1;
   Eina_Bool taskbar : 1;

   Eina_Bool resize : 1;
} E_Smart_Data;

typedef struct Mirror
{
   EINA_INLIST;
   E_Smart_Data *sd;
   E_Client *ec;
   Evas_Object *comp_object;
   Evas_Object *mirror;
   int x, y, w, h;
   Eina_Bool frame : 1;
   Eina_Bool added : 1;
} Mirror;

typedef struct Mirror_Border
{
   Mirror *m;
   Evas_Object *mirror;
   Evas_Object *frame;
   Evas_Object *obj;
} Mirror_Border;

static Evas_Smart *_e_deskmirror_smart = NULL;
static Evas_Smart *_mirror_client_smart = NULL;

static void _e_deskmirror_mirror_setup(Mirror *m);
static void _comp_object_hide(Mirror *m, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED);
static void _comp_object_show(Mirror *m, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED);
static void _comp_object_stack(Mirror *m, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED);
static void _comp_object_configure(Mirror *m, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED);

static Eina_Bool
_e_deskmirror_visible_get(E_Smart_Data *sd, Mirror *m)
{
   Eina_Bool visible = evas_object_visible_get(m->comp_object);
   if (m->ec)
     {
        visible = m->ec->visible;
        /* iconic flips meaning of visible flag at this point
         * where "this point" is defined as being during the show
         * callback for a client
         */
        if (m->sd->handlers && m->ec->iconic)
          visible = !m->ec->visible;
        if (visible)
          {
             visible = (sd->desk == m->ec->desk) || (m->ec->sticky);
             if (sd->pager)
               visible = !m->ec->netwm.state.skip_pager;
             if (visible && sd->taskbar)
               visible = !m->ec->netwm.state.skip_taskbar;
          }
     }
   if ((m->w < 1) || (m->h < 1)) visible = EINA_FALSE;
   return visible;
}

static void
_mirror_visible_apply(Mirror *m)
{
   if (_e_deskmirror_visible_get(m->sd, m))
     evas_object_show(m->mirror);
   else
     evas_object_hide(m->mirror);
}

static void
_mirror_scale_set(Mirror *m, float sc)
{
   Edje_Message_Float_Set msg;
   Mirror_Border *mb;

   /* FIXME: broken */
   return;
   if (!m->frame) return;

   mb = evas_object_smart_data_get(m->mirror);
   msg.count = 1;
   msg.val[0] = sc;
   edje_object_message_send(mb->frame, EDJE_MESSAGE_FLOAT_SET, 0, &msg);
}

static void
_e_deskmirror_smart_reconfigure(E_Smart_Data *sd)
{
   e_layout_freeze(sd->layout);
   evas_object_move(sd->clip, sd->x, sd->y);
   evas_object_move(sd->bgpreview, sd->x, sd->y);
   evas_object_move(sd->layout, sd->x, sd->y);

   if (sd->resize)
     {
        Mirror *m;

        evas_object_resize(sd->clip, sd->w, sd->h);
        evas_object_resize(sd->bgpreview, sd->w, sd->h);
        evas_object_resize(sd->layout, sd->w, sd->h);
        EINA_INLIST_FOREACH(sd->mirrors, m)
          _mirror_scale_set(m, (float)sd->h / (float)sd->desk->zone->h);
     }
   e_layout_thaw(sd->layout);
   sd->resize = 0;
}

///////////////////////////////////////////////

static void
_e_deskmirror_smart_add(Evas_Object *obj)
{
   E_Smart_Data *sd;

   sd = E_NEW(E_Smart_Data, 1);
   if (!sd) return;
   sd->obj = obj;
   sd->e = evas_object_evas_get(obj);
   sd->x = sd->y = sd->w = sd->h = 0;
   sd->clip = evas_object_rectangle_add(sd->e);
   evas_object_smart_member_add(sd->clip, sd->obj);
   evas_object_smart_data_set(obj, sd);
}

static void
_e_deskmirror_smart_del(Evas_Object *obj)
{
   INTERNAL_ENTRY;
   e_object_unref(E_OBJECT(sd->desk));
   if (sd->desk_delfn)
     {
        e_object_delfn_del(E_OBJECT(sd->desk), sd->desk_delfn);
        sd->desk_delfn = NULL;
        sd->desk = NULL;
     }
   E_FREE_LIST(sd->handlers, ecore_event_handler_del);
   eina_hash_free(sd->mirror_hash);
   evas_object_del(sd->clip);
   evas_object_del(sd->bgpreview);
   evas_object_del(sd->layout);
   free(sd);
}

static void
_e_deskmirror_smart_move(Evas_Object *obj, Evas_Coord x, Evas_Coord y)
{
   INTERNAL_ENTRY;
   sd->x = x;
   sd->y = y;
   _e_deskmirror_smart_reconfigure(sd);
}

static void
_e_deskmirror_smart_resize(Evas_Object *obj, Evas_Coord w, Evas_Coord h)
{
   INTERNAL_ENTRY;
   sd->w = w;
   sd->h = h;
   sd->resize = 1;
   _e_deskmirror_smart_reconfigure(sd);
}

static void
_e_deskmirror_smart_show(Evas_Object *obj)
{
   INTERNAL_ENTRY;
   evas_object_show(sd->clip);
}

static void
_e_deskmirror_smart_hide(Evas_Object *obj)
{
   INTERNAL_ENTRY;
   evas_object_hide(sd->clip);
}

static void
_e_deskmirror_smart_color_set(Evas_Object *obj, int r, int g, int b, int a)
{
   INTERNAL_ENTRY;
   evas_object_color_set(sd->clip, r, g, b, a);
}

static void
_e_deskmirror_smart_clip_set(Evas_Object *obj, Evas_Object *clip)
{
   INTERNAL_ENTRY;
   evas_object_clip_set(sd->clip, clip);
}

static void
_e_deskmirror_smart_clip_unset(Evas_Object *obj)
{
   INTERNAL_ENTRY;
   evas_object_clip_unset(sd->clip);
}

////////////////////////////////////////////////////////

static void
_e_deskmirror_mirror_frame_recalc_cb(void *data, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   Mirror *m = data;
   Mirror_Border *mb;

   
   if (m->frame)
     {
        mb = evas_object_smart_data_get(m->mirror);
        if (mb->m->ec && (!e_object_is_del(E_OBJECT(mb->m->ec))))
          {
             if (e_comp_object_frame_exists(obj)) return;
             evas_object_smart_member_del(mb->mirror);
             mb->m->mirror = mb->mirror;
             mb->mirror = NULL;
             mb->m->frame = 0;
          }
        else
          mb->m->comp_object = NULL;
        evas_object_del(mb->obj);
        e_layout_pack(m->sd->layout, m->mirror);
     }
   else
     {
        if (m->ec && (!e_object_is_del(E_OBJECT(m->ec))))
          {
             if (!e_comp_object_frame_exists(obj)) return;
             e_layout_unpack(m->mirror);
             _e_deskmirror_mirror_setup(m);
          }
     }
}

static void
_mirror_client_smart_add(Evas_Object *obj)
{
   Mirror_Border *mb;

   mb = E_NEW(Mirror_Border, 1);
   mb->obj = obj;
   evas_object_smart_data_set(obj, mb);
}

static void
_mirror_client_signal_cb(void *data, Evas_Object *obj EINA_UNUSED, const char *emission, const char *src)
{
   Mirror_Border *mb = data;
   edje_object_signal_emit(mb->frame, emission, src);
   edje_object_message_signal_process(mb->frame);
   edje_object_calc_force(mb->frame);
}

static void
_mirror_client_smart_del(Evas_Object *obj)
{
   Mirror_Border *mb = evas_object_smart_data_get(obj);
   if (mb->m->comp_object && mb->m->ec)
     e_comp_object_signal_callback_del_full(mb->m->ec->frame, "*", "*", _mirror_client_signal_cb, mb);
   evas_object_del(mb->frame);
   evas_object_del(mb->mirror);
   free(mb);
}

static void
_mirror_client_smart_move(Evas_Object *obj, Evas_Coord x, Evas_Coord y)
{
   Mirror_Border *mb = evas_object_smart_data_get(obj);

   evas_object_move(mb->frame, x, y);
}

static void
_mirror_client_smart_resize(Evas_Object *obj, Evas_Coord w, Evas_Coord h)
{
   Mirror_Border *mb = evas_object_smart_data_get(obj);
   evas_object_resize(mb->frame, w, h);
}

static void
_mirror_client_smart_show(Evas_Object *obj)
{
   Mirror_Border *mb = evas_object_smart_data_get(obj);
   evas_object_show(mb->frame);
}

static void
_mirror_client_smart_hide(Evas_Object *obj)
{
   Mirror_Border *mb = evas_object_smart_data_get(obj);
   evas_object_hide(mb->frame);
}

static void
_mirror_client_smart_color_set(Evas_Object *obj, int r, int g, int b, int a)
{
   Mirror_Border *mb = evas_object_smart_data_get(obj);
   evas_object_color_set(mb->frame, r, g, b, a);
}

static void
_mirror_client_smart_clip_set(Evas_Object *obj, Evas_Object *clip)
{
   Mirror_Border *mb = evas_object_smart_data_get(obj);
   evas_object_clip_set(mb->frame, clip);
}

static void
_mirror_client_smart_clip_unset(Evas_Object *obj)
{
   Mirror_Border *mb = evas_object_smart_data_get(obj);
   evas_object_clip_unset(mb->frame);
}

static void
_mirror_client_smart_init(void)
{
   static const Evas_Smart_Class sc =
     {
        "mirror_border", EVAS_SMART_CLASS_VERSION,
        _mirror_client_smart_add, _mirror_client_smart_del, _mirror_client_smart_move, _mirror_client_smart_resize,
        _mirror_client_smart_show, _mirror_client_smart_hide, _mirror_client_smart_color_set, _mirror_client_smart_clip_set,
        _mirror_client_smart_clip_unset, NULL, NULL, NULL, NULL, NULL, NULL, NULL
     };
   if (_mirror_client_smart) return;
   _mirror_client_smart = evas_smart_class_new(&sc);
}

static void
_e_deskmirror_smart_init(void)
{
   static const Evas_Smart_Class sc =
     {
        SMART_NAME, EVAS_SMART_CLASS_VERSION,
        _e_deskmirror_smart_add, _e_deskmirror_smart_del, _e_deskmirror_smart_move, _e_deskmirror_smart_resize,
        _e_deskmirror_smart_show, _e_deskmirror_smart_hide, _e_deskmirror_smart_color_set, _e_deskmirror_smart_clip_set,
        _e_deskmirror_smart_clip_unset, NULL, NULL, NULL, NULL, NULL, NULL, NULL
     };
   if (_e_deskmirror_smart) return;
   _e_deskmirror_smart = evas_smart_class_new(&sc);
}

static void
_e_deskmirror_delfn(E_Smart_Data *sd, void *desk EINA_UNUSED)
{
   sd->desk_delfn = NULL;
   sd->desk = NULL;
   evas_object_del(sd->obj);
}

static void
_e_deskmirror_mirror_del(Mirror *m)
{
   eina_hash_del_by_key(m->sd->mirror_hash, &m->comp_object);
}

static void
_e_deskmirror_mirror_del_cb(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   _e_deskmirror_mirror_del(data);
}

static void
_e_deskmirror_mirror_geometry_get(Mirror *m)
{
   E_Zone *zone;

   evas_object_geometry_get(m->comp_object, &m->x, &m->y, &m->w, &m->h);
   zone = e_comp_object_util_zone_get(m->comp_object);
   if (zone)
     m->x -= zone->x, m->y -= zone->y;
   /* double check here if we get zeroes */
   if ((!m->w) || (!m->h))
     {
        ERR("ACK!");
     }
}

static void
_e_deskmirror_mirror_reconfigure(Mirror *m)
{
   _e_deskmirror_mirror_geometry_get(m);
   e_layout_child_move(m->mirror, m->x, m->y);
   e_layout_child_resize(m->mirror, m->w, m->h);
   _mirror_visible_apply(m);
}

static void
_e_deskmirror_mirror_del_hash(Mirror *m)
{
   m->sd->mirrors = eina_inlist_remove(m->sd->mirrors, EINA_INLIST_GET(m));
   evas_object_smart_callback_del_full(m->comp_object, "frame_recalc_done", _e_deskmirror_mirror_frame_recalc_cb, m);
   evas_object_event_callback_del_full(m->comp_object, EVAS_CALLBACK_DEL, _e_deskmirror_mirror_del_cb, m);
   evas_object_del(m->mirror);
   evas_object_event_callback_del_full(m->comp_object, EVAS_CALLBACK_SHOW, (Evas_Object_Event_Cb)_comp_object_show, m);
   evas_object_event_callback_del_full(m->comp_object, EVAS_CALLBACK_HIDE, (Evas_Object_Event_Cb)_comp_object_hide, m);
   evas_object_event_callback_del_full(m->comp_object, EVAS_CALLBACK_RESTACK, (Evas_Object_Event_Cb)_comp_object_stack, m);
   evas_object_event_callback_del_full(m->comp_object, EVAS_CALLBACK_RESIZE, (Evas_Object_Event_Cb)_comp_object_configure, m);
   evas_object_event_callback_del_full(m->comp_object, EVAS_CALLBACK_MOVE, (Evas_Object_Event_Cb)_comp_object_configure, m);
   free(m);
}

static Evas_Object *
_mirror_client_new(Mirror *m)
{
   Evas_Object *o;
   Mirror_Border *mb;
   char buf[4096];

   _mirror_client_smart_init();
   o = evas_object_smart_add(m->sd->e, _mirror_client_smart);
   mb = evas_object_smart_data_get(o);
   mb->m = m;
   mb->frame = edje_object_add(m->sd->e);
   evas_object_name_set(mb->frame, "mirror_border");
   snprintf(buf, sizeof(buf), "e/deskmirror/frame/%s", m->ec->border.name);
   e_theme_edje_object_set(mb->frame, "base/theme/borders", buf);
   if (e_util_client_shadow_state_get(m->ec))
     edje_object_signal_emit(mb->frame, "e,state,shadow,on", "e");
   else
     edje_object_signal_emit(mb->frame, "e,state,shadow,off", "e");
   if (m->comp_object)
     {
        e_comp_object_signal_callback_add(mb->m->comp_object, "*", "*", _mirror_client_signal_cb, mb);
        evas_object_event_callback_add(m->comp_object, EVAS_CALLBACK_DEL, _e_deskmirror_mirror_del_cb, m);
     }
   if (mb->m->ec->focused)
     edje_object_signal_emit(mb->frame, "e,state,focused", "e");
   if (mb->m->ec->shaded)
     edje_object_signal_emit(mb->frame, "e,state,shaded", "e");
   if (mb->m->ec->maximized)
     edje_object_signal_emit(mb->frame, "e,action,maximize", "e");
   if (mb->m->ec->sticky)
     edje_object_signal_emit(mb->frame, "e,state,sticky", "e");
   if (mb->m->ec->iconic)
     edje_object_signal_emit(mb->frame, "e,action,iconify", "e");

   mb->mirror = m->mirror;
   evas_object_smart_member_add(mb->frame, o);
   evas_object_name_set(mb->mirror, "mirror");
   edje_object_part_swallow(mb->frame, "e.swallow.client", m->mirror);
   edje_object_part_text_set(mb->frame, "e.text.title", e_client_name_get(m->ec));
   if (m->sd->h)
     _mirror_scale_set(m, (double)m->sd->h / (double)m->sd->desk->zone->h);
   return o;
}

static void
_e_deskmirror_mirror_setup(Mirror *m)
{
   if (!m->mirror) return;
   evas_object_event_callback_del_full(m->comp_object, EVAS_CALLBACK_DEL, _e_deskmirror_mirror_del_cb, m);
   if (m->ec && e_comp_object_frame_exists(m->comp_object))
     {
        m->mirror = _mirror_client_new(m);
        m->frame = 1;
     }
   else
     {
        evas_object_pass_events_set(m->mirror, !m->ec);
        evas_object_event_callback_add(m->comp_object, EVAS_CALLBACK_DEL, _e_deskmirror_mirror_del_cb, m);
     }
   if (m->ec) evas_object_data_set(m->mirror, "E_Client", m->ec);
   evas_object_precise_is_inside_set(m->mirror, m->ec && m->ec->shaped);
   e_layout_pack(m->sd->layout, m->mirror);
   _e_deskmirror_mirror_reconfigure(m);
   if (m->sd->handlers) // no handlers = we're setting up = there's no possible listeners
     {
        _comp_object_stack(m, NULL, m->comp_object, NULL);
        if (!m->added)
          evas_object_smart_callback_call(m->sd->obj, "mirror_add", m->mirror);
     }
   m->added = 1;
}

static Eina_Bool
_comp_object_check(Mirror *m)
{
   int w, h;

   evas_object_geometry_get(m->comp_object, NULL, NULL, &w, &h);
   if ((w < 2) || (h < 2)) return EINA_FALSE;
   m->mirror = e_comp_object_util_mirror_add(m->comp_object);
   evas_object_name_set(m->mirror, "m->mirror");
   if (m->mirror) evas_object_layer_set(m->mirror, evas_object_layer_get(m->comp_object));
   _e_deskmirror_mirror_setup(m);
   return EINA_TRUE;
}

static void
_comp_object_hide(Mirror *m, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   _mirror_visible_apply(m);
}

static void
_comp_object_show(Mirror *m, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   if (!m->mirror)
     {
        if (!_comp_object_check(m)) return;
     }
   _mirror_visible_apply(m);
}

static void
_comp_object_stack(Mirror *m, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   Evas_Object *comp_object;

   if (m->ec && m->ec->layer_block) return; //ignore layer blockers

   if (!m->mirror)
     {
        if (!_comp_object_check(m)) return;
     }
   comp_object = evas_object_below_get(obj);
   while (comp_object)
     {
        Mirror *m2;

        m2 = eina_hash_find(m->sd->mirror_hash, &comp_object);
        if (m2 && m2->mirror)
          {
             e_layout_child_raise_above(m->mirror, m2->mirror);
             return;
          }
        comp_object = evas_object_below_get(comp_object);
     }
   e_layout_child_lower(m->mirror);
}

static void
_comp_object_configure(Mirror *m, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   if (!m->mirror)
     {
        if (!_comp_object_check(m)) return;
     }
   _e_deskmirror_mirror_reconfigure(m);
}

static Mirror *
_e_deskmirror_mirror_add(E_Smart_Data *sd, Evas_Object *obj)
{
   Mirror *m;
   Evas_Object *o = NULL;
   E_Client *ec;
   int w, h;

   if (sd->desk->zone != e_comp_object_util_zone_get(obj)) return NULL;

   ec = e_comp_object_client_get(obj);
   if (ec)
     {
        if ((ec->desk != sd->desk) && (!ec->sticky))
          return NULL;
        if (ec->input_only || ec->ignored)
          return NULL;
     }
   else
     {
        E_Shelf *es;

        if (sd->pager || sd->taskbar) return NULL;
        es = evas_object_data_get(obj, "E_Shelf");
        if (es)
          {
             if (!e_shelf_desk_visible(es, sd->desk)) return NULL;
          }
        else
          {
             if (sd->desk != e_desk_current_get(sd->desk->zone)) return NULL;
          }
     }
   evas_object_geometry_get(obj, NULL, NULL, &w, &h);
   if ((w > 1) && (h > 1))
     {
        o = e_comp_object_util_mirror_add(obj);
        evas_object_name_set(o, "m->mirror");
     }
   m = calloc(1, sizeof(Mirror));
   m->comp_object = obj;
   m->ec = ec;
   m->sd = sd;
   m->mirror = o;
   if (o) evas_object_layer_set(o, evas_object_layer_get(obj));
   evas_object_event_callback_add(obj, EVAS_CALLBACK_SHOW, (Evas_Object_Event_Cb)_comp_object_show, m);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_HIDE, (Evas_Object_Event_Cb)_comp_object_hide, m);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_RESTACK, (Evas_Object_Event_Cb)_comp_object_stack, m);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_RESIZE, (Evas_Object_Event_Cb)_comp_object_configure, m);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_MOVE, (Evas_Object_Event_Cb)_comp_object_configure, m);
   evas_object_smart_callback_add(obj, "frame_recalc_done", _e_deskmirror_mirror_frame_recalc_cb, m);
   sd->mirrors = eina_inlist_append(sd->mirrors, EINA_INLIST_GET(m));
   eina_hash_add(sd->mirror_hash, &obj, m);
   _e_deskmirror_mirror_setup(m);
   return m;
}

static Eina_Bool
_comp_object_add(E_Smart_Data *sd, int type EINA_UNUSED, E_Event_Comp_Object *ev)
{
   if (eina_hash_find(sd->mirror_hash, &ev->comp_object)) return ECORE_CALLBACK_RENEW;
   _e_deskmirror_mirror_add(sd, ev->comp_object);
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_client_desk_set(E_Smart_Data *sd, int type EINA_UNUSED, E_Event_Client_Desk_Set *ev)
{
   Mirror *m;

   m = eina_hash_find(sd->mirror_hash, &ev->ec->frame);
   if (m)
     {
        /* ev->desk is previous desk */
        if ((ev->desk == sd->desk) && (!ev->ec->sticky))
          eina_hash_del_by_key(sd->mirror_hash, &ev->ec->frame);
     }
   if (sd->desk == ev->ec->desk)
     _e_deskmirror_mirror_add(sd, ev->ec->frame);
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_client_property(E_Smart_Data *sd, int type EINA_UNUSED, E_Event_Client_Property *ev)
{
   Mirror *m;

   if (!(ev->property & E_CLIENT_PROPERTY_NETWM_STATE)) return ECORE_CALLBACK_RENEW;

   m = eina_hash_find(sd->mirror_hash, &ev->ec->frame);
   if (m) _mirror_visible_apply(m);
   return ECORE_CALLBACK_RENEW;
}

/* externally accessible functions */
EAPI Evas_Object *
e_deskmirror_add(E_Desk *desk, Eina_Bool pager, Eina_Bool taskbar)
{
   E_Smart_Data *sd;
   Evas_Object *o, *l;
   Evas *e;

   e = e_comp_get(desk)->evas;
   _e_deskmirror_smart_init();
   o = evas_object_smart_add(e, _e_deskmirror_smart);
   e_object_ref(E_OBJECT(desk));
   sd = evas_object_smart_data_get(o);
   sd->pager = !!pager;
   sd->taskbar = !!taskbar;
   sd->desk = desk;
   sd->mirror_hash = eina_hash_pointer_new((Eina_Free_Cb)_e_deskmirror_mirror_del_hash);
   sd->desk_delfn = e_object_delfn_add(E_OBJECT(desk), (Ecore_End_Cb)_e_deskmirror_delfn, sd);
   sd->bgpreview = e_widget_bgpreview_desk_add(e, desk->zone, desk->x, desk->y);
   evas_object_pass_events_set(sd->bgpreview, 1);
   evas_object_clip_set(sd->bgpreview, sd->clip);
   evas_object_smart_member_add(sd->bgpreview, o);
   evas_object_show(sd->bgpreview);
   sd->layout = e_layout_add(e);
   evas_object_clip_set(sd->layout, sd->clip);
   e_layout_virtual_size_set(sd->layout, desk->zone->w, desk->zone->h);
   evas_object_smart_member_add(sd->layout, o);
   evas_object_show(sd->layout);

   e_comp_object_util_center_on(o, sd->desk->zone->bg_clip_object);

   e_layout_freeze(sd->layout);

   l = evas_object_bottom_get(e_comp_get(desk)->evas);
   do
     {
        if (evas_object_data_get(l, "comp_object"))
          _e_deskmirror_mirror_add(sd, l);
        l = evas_object_above_get(l);
     } while (l);

   e_layout_thaw(sd->layout);

   E_LIST_HANDLER_APPEND(sd->handlers, E_EVENT_COMP_OBJECT_ADD, (Ecore_Event_Handler_Cb)_comp_object_add, sd);
   E_LIST_HANDLER_APPEND(sd->handlers, E_EVENT_CLIENT_PROPERTY, (Ecore_Event_Handler_Cb)_client_property, sd);
   E_LIST_HANDLER_APPEND(sd->handlers, E_EVENT_CLIENT_DESK_SET, (Ecore_Event_Handler_Cb)_client_desk_set, sd);
   return o;
}

EAPI Evas_Object *
e_deskmirror_mirror_find(Evas_Object *deskmirror, Evas_Object *comp_object)
{
   Mirror *m;
   EINA_SAFETY_ON_NULL_RETURN_VAL(deskmirror, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(comp_object, NULL);
   API_ENTRY(deskmirror) NULL;

   m = eina_hash_find(sd->mirror_hash, &comp_object);
   return m ? m->mirror : NULL;
}

EAPI Eina_List *
e_deskmirror_mirror_list(Evas_Object *deskmirror)
{
   Eina_List *l = NULL;
   Mirror *m;

   EINA_SAFETY_ON_NULL_RETURN_VAL(deskmirror, NULL);
   API_ENTRY(deskmirror) NULL;

   EINA_INLIST_FOREACH(sd->mirrors, m)
     {
        if (m->mirror)
          l = eina_list_append(l, m->mirror);
     }
   return l;
}

EAPI void
e_deskmirror_coord_canvas_to_virtual(Evas_Object *obj, Evas_Coord cx, Evas_Coord cy, Evas_Coord *vx, Evas_Coord *vy)
{
   API_ENTRY(obj);

   e_layout_coord_canvas_to_virtual(sd->layout, cx, cy, vx, vy);
}

EAPI void
e_deskmirror_coord_virtual_to_canvas(Evas_Object *obj, Evas_Coord vx, Evas_Coord vy, Evas_Coord *cx, Evas_Coord *cy)
{
   API_ENTRY(obj);

   e_layout_coord_virtual_to_canvas(sd->layout, vx, vy, cx, cy);
}

EAPI E_Desk *
e_deskmirror_desk_get(Evas_Object *obj)
{
   API_ENTRY(obj) NULL;
   return sd->desk;
}

EAPI void
e_deskmirror_util_wins_print(Evas_Object *obj)
{
   E_Smart_Data *sd;
   Mirror *m;

   EINA_SAFETY_ON_NULL_RETURN(obj);
   sd = evas_object_smart_data_get(obj);
   EINA_INLIST_FOREACH(sd->mirrors, m)
     {
        if (m->ec)
          {
             if (m->ec->override)
               fprintf(stderr, "MIRROR OVERRIDE: %p - %p%s\n", m->comp_object, m->ec, m->ec->input_only ? " INPUT" : "");
             else
               fprintf(stderr, "MIRROR EC:  %p - %p '%s:%s'\n", m->comp_object, m->ec, m->ec->icccm.name, m->ec->icccm.class);
          }
        else
          fprintf(stderr, "MIRROR OBJ: %p\n", m->comp_object);
     }
}
