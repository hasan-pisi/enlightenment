#include "e_mod_main.h"

EINTERN int _e_lokker_log_dom = -1;

EAPI E_Module_Api e_modapi = {E_MODULE_API_VERSION, "Lokker"};
static E_Desklock_Interface lokker_desklock_iface =
{
   .name = "lokker",
   .show = lokker_lock,
   .hide = lokker_unlock
};

EAPI void *
e_modapi_init(E_Module *m)
{
   _e_lokker_log_dom = eina_log_domain_register("lokker", EINA_COLOR_ORANGE);
   eina_log_domain_level_set("lokker", EINA_LOG_LEVEL_DBG);

   e_desklock_interface_append(&lokker_desklock_iface);

   if (e_desklock_state_get())
     lokker_lock();

   return m;
}

EAPI int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
   e_desklock_interface_remove(&lokker_desklock_iface);

   eina_log_domain_unregister(_e_lokker_log_dom);
   _e_lokker_log_dom = -1;

   return 1;
}
