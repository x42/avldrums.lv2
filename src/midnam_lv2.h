#define LV2_MIDNAM_URI "http://ardour.org/lv2/midnam"
#define LV2_MIDNAM_PREFIX LV2_MIDNAM_URI "#"
#define LV2_MIDNAM__interface LV2_MIDNAM_PREFIX "interface"
#define LV2_MIDNAM__update LV2_MIDNAM_PREFIX "update"

typedef void* LV2_Midnam_Handle;

/** a LV2 Feature provided by the Host to the plugin */
typedef struct {
  /** Opaque host data */
  LV2_Midnam_Handle handle;
  /** Request from run() that the host should re-read the midnam */
  void (*update)(LV2_Midnam_Handle handle);
} LV2_Midnam;

typedef struct {
  /** query midnam document. The plugin
   * is expected to return an allocated
   * null-terminated XML text, which is
   * safe for the host to pass to free().
   *
   * The midnam <Model> must be unique and
   * specific for the given plugin-instance.
   */
  char* (*midnam)(LV2_Handle instance);
  char* (*model)(LV2_Handle instance);
} LV2_Midnam_Interface;
