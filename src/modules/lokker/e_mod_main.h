#ifndef E_MOD_MAIN_H
#define E_MOD_MAIN_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e.h"

#undef DBG
#undef INF
#undef WRN
#undef ERR
#undef CRIT
#define DBG(...)  EINA_LOG_DOM_DBG(_e_lokker_log_dom, __VA_ARGS__)
#define INF(...)  EINA_LOG_DOM_INFO(_e_lokker_log_dom, __VA_ARGS__)
#define WRN(...)  EINA_LOG_DOM_WARN(_e_lokker_log_dom, __VA_ARGS__)
#define ERR(...)  EINA_LOG_DOM_ERR(_e_lokker_log_dom, __VA_ARGS__)
#define CRIT(...) EINA_LOG_DOM_CRIT(_e_lokker_log_dom, __VA_ARGS__)

EINTERN Eina_Bool lokker_lock(void);
EINTERN void lokker_unlock(void);

#endif
