/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <zmk-input-inertia/runtime.h>
#define ARG_UNUSED(x) ((void)(x))
#define K_FOREVER 0
struct k_mutex { int depth; };
#define K_MUTEX_DEFINE(n) struct k_mutex n
static int k_mutex_lock(struct k_mutex *m,int timeout) { ARG_UNUSED(timeout); m->depth++;return 0; }
static int k_mutex_unlock(struct k_mutex *m) { assert(m->depth>0);m->depth--;return 0; }
#define CONFIG_ZMK_INPUT_INERTIA_LOG_LEVEL 3
#define CONFIG_ZMK_CUSTOM_SETTINGS_KEY_MAX_LEN 48
#define SETTINGS_MAX_NAME_LEN 64
#define STR_(x) #x
#define STRINGIFY(x) STR_(x)
#define BUILD_ASSERT(c,...) _Static_assert(c,__VA_ARGS__)
#define DT_DRV_INST(n) n
#define CAT_(a,b) a##b
#define CAT(a,b) CAT_(a,b)
#if defined(TEST_LONG_STORAGE)
#define name_0 "a_node_that_fits_key_limit"
#elif defined(TEST_LONG_KEY)
#define name_0 "a_node_name_that_is_far_too_long_for_the_public_setting_key"
#else
#define name_0 "glide_a"
#endif
#define name_1 "scroll_inertia"
#define DT_NODE_FULL_NAME(n) CAT(name_,n)
#define DT_INST_FOREACH_STATUS_OKAY(F) F(0) F(1)
#define DT_INST_PROP(n,f) CAT(prop_,f)
#define prop_trigger_ms 35
#define prop_move_decay_factor_int 90
#define prop_move_report_interval_ms 35
#define prop_move_threshold_start 15
#define prop_move_threshold_stop 1
#define prop_scroll_decay_factor_int 85
#define prop_scroll_report_interval_ms 65
#define prop_scroll_threshold_start 2
#define prop_scroll_threshold_stop 0
#define prop_cancel_scroll_inertia_on_ctrl 0
struct device { int index; };
static const struct device devices[2]={{0},{1}};
#define DEVICE_DT_INST_GET(n) (&devices[n])
struct zmk_custom_setting_value { int type; int32_t int32_value; bool bool_value; };
struct range { int32_t lo,hi; };
struct zmk_custom_setting { const char *custom_subsystem_id,*key; int type; struct zmk_custom_setting_value value; struct range range; int error; };
#define ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32 2
#define ZMK_CUSTOM_SETTING_VALUE_TYPE_BOOL 3
#define ZMK_CUSTOM_SETTING_VALUE_INT32(v) ((struct zmk_custom_setting_value){.type=2,.int32_value=(v)})
#define ZMK_CUSTOM_SETTING_VALUE_BOOL(v) ((struct zmk_custom_setting_value){.type=3,.bool_value=(v)})
#define ZMK_CUSTOM_SETTING_RANGE_INT32(a,b) ((struct range){a,b})
#define ZMK_CUSTOM_SETTING_NO_CONSTRAINT ((struct range){0,0})
#define ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(n,sub,k,t,v,c,r,w,...) \
 static struct zmk_custom_setting n={.custom_subsystem_id=sub,.key=k,.type=t,.value=v,.range=__VA_ARGS__}
static int zmk_custom_setting_read(const struct zmk_custom_setting *s,struct zmk_custom_setting_value *out) { if(s->error)return -1;*out=s->value;return 0; }
static struct zmk_custom_setting *registry[21];
#define ZMK_CUSTOM_SETTING_FOREACH(s) for(size_t s##_i=0;s##_i<21;s##_i++) for(struct zmk_custom_setting *s=registry[s##_i];s;s=NULL)
struct zmk_custom_setting_changed { const struct zmk_custom_setting *setting; };
typedef struct { const struct zmk_custom_setting_changed *changed; } zmk_event_t;
static const struct zmk_custom_setting_changed *as_zmk_custom_setting_changed(const zmk_event_t *e) { return e->changed; }
#define ZMK_EV_EVENT_BUBBLE 0
#define ZMK_LISTENER(n,cb) static int (*n##_callback)(const zmk_event_t *) __attribute__((used))=cb
#define ZMK_SUBSCRIPTION(...)
#define SYS_INIT(fn,...) static int (*fn##_init)(void) __attribute__((used))=fn
#define ZMK_STUDIO_RPC_HANDLER_UNSECURED 0
#define ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS(u) .url=u
struct zmk_rpc_custom_subsystem_meta { const char *url; int security; };
typedef struct { int unused; } zmk_custom_CallRequest;
typedef struct { int unused; } pb_callback_t;
#define ZMK_RPC_CUSTOM_SUBSYSTEM(token,meta,handler) static const void *const registration[] __attribute__((used))={meta,(void *)handler}
#define LOG_MODULE_REGISTER(...)
static int warnings,errors;
#define LOG_WRN(...) ((void)warnings++)
#define LOG_ERR(...) ((void)errors++)
static struct inertia_params live[2];
static int applies[2],reject;
int inertia_set_params(const struct device *dev,const struct inertia_params *p) {
 applies[dev->index]++;if(reject)return -1;
#ifdef TEST_IGNORE_APPLY
 if(p->trigger_ms==40)return 0;
#endif
 live[dev->index]=*p;return 0;
}
#include "../../src/input_processor_inertia_custom_settings.c"
#define REG_INT(n,f,key,lo,hi) registry[count++]=&inertia_cs_##f##_##n;
#define REG_BOOL(n,f,key) registry[count++]=&inertia_cs_##f##_##n;
int main(void) {
 size_t count=0;
 INERTIA_INT_FIELDS(REG_INT,0) INERTIA_BOOL_FIELDS(REG_BOOL,0)
 INERTIA_INT_FIELDS(REG_INT,1) INERTIA_BOOL_FIELDS(REG_BOOL,1)
 assert(count==20);
 assert(sizeof(INERTIA_STORAGE_NAME(1,"cancel_on_ctrl"))==SETTINGS_MAX_NAME_LEN);
 assert(strcmp(ZMK_INPUT_INERTIA_SUBSYSTEM,"amgskobo__inertia") == 0);
 for (size_t i=0;i<count;i++) assert(strcmp(registry[i]->custom_subsystem_id,ZMK_INPUT_INERTIA_SUBSYSTEM)==0);
 assert(strcmp(inertia_cs_move_report_interval_ms_0.key,"glide_a.move_period")==0);
 assert(strcmp(inertia_cs_scroll_report_interval_ms_1.key,"scroll_inertia.scroll_period")==0);
 struct zmk_custom_setting unrelated={.custom_subsystem_id="other",.key="unrelated"};registry[count]=&unrelated;
 assert(!inertia_namespace_handler(NULL,NULL));
 assert(inertia_check_keys()==0 && errors==0);
 assert(strcmp(inertia_cs_trigger_ms_0.key,"glide_a.trigger_ms")==0);
 assert(strcmp(inertia_cs_cancel_scroll_inertia_on_ctrl_1.key,"scroll_inertia.cancel_on_ctrl")==0);
 assert(inertia_cs_move_decay_factor_int_0.range.hi==99 && inertia_cs_move_threshold_start_0.range.lo==1);
 assert(inertia_cs_scroll_report_interval_ms_0.range.hi==65535);
 zmk_event_t init={0};assert(inertia_settings_event(&init)==0);
 assert(applies[0]==1 && applies[1]==1 && live[0].trigger_ms==35 && live[1].move_decay_factor_int==90);
 struct zmk_custom_setting_changed changed={&unrelated};zmk_event_t event={&changed};
 assert(inertia_settings_event(&event)==0 && applies[0]==1);
 inertia_cs_trigger_ms_0.value.int32_value=40;inertia_cs_cancel_scroll_inertia_on_ctrl_1.value.bool_value=true;
 changed.setting=&inertia_cs_trigger_ms_0;
 assert(inertia_settings_event(&event)==0 && live[0].trigger_ms==40 && live[1].cancel_scroll_inertia_on_ctrl && "Inertia settings apply");
 for(size_t i=0;i<20;i++) {
  int before=applies[i/10];registry[i]->error=-1;
  inertia_apply_settings();assert(applies[i/10]==before);registry[i]->error=0;
  int type=registry[i]->value.type;registry[i]->value.type=99;
  inertia_apply_settings();assert(applies[i/10]==before);registry[i]->value.type=type;
 }
 reject=1;inertia_apply_settings();assert(warnings==2);reject=0;
 const char *key=registry[1]->key;registry[1]->key=registry[0]->key;
 assert(inertia_check_keys()==0 && errors>=1);registry[1]->key=key;
 const char *sub=registry[0]->custom_subsystem_id;registry[0]->custom_subsystem_id="other";
 assert(inertia_check_keys()==0);registry[0]->custom_subsystem_id=sub;
 assert(settings_lock.depth==0);
 puts("Inertia custom settings: 20 keys/defaults/ranges, init, two nodes, read/type errors, rejected snapshots and duplicates PASS");return 0;
}
