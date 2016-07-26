#include <pebble.h>

enum {
  STORAGE_WATERCNT_ML =23,
  STORAGE_DAY,
  STORAGE_GOAL_ML,
};

#define DEFAULT_GOAL 5000

static int g_watercnt_ml = 0;
static int g_goal = DEFAULT_GOAL;
static char g_day[PERSIST_STRING_MAX_LENGTH] = { '\0' };


void storeData() {
  persist_write_int(STORAGE_WATERCNT_ML, g_watercnt_ml);
  persist_write_int(STORAGE_GOAL_ML, g_goal);
  persist_write_string(STORAGE_DAY, g_day);
}

static Window *s_main_window; 
static TextLayer *s_time_layer;
static TextLayer *s_subtext;

static void update_time() {
  // Get a tm structure
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  
  static char curDay[8];
  strftime(curDay, sizeof(curDay), "%m-%d", tick_time); // %F = "%Y-%m-%d" ... but the year doesn't matter (unless you don't use the app for exactly one year °°)
  if(0 != strcmp(curDay,g_day)) {
    if(g_watercnt_ml > DEFAULT_GOAL/2) {
      g_goal = g_watercnt_ml;
    }
    g_watercnt_ml = 0;
    strcpy(g_day,curDay);
    storeData();
  }

//  int curShould = (g_goal * tick_time->tm_hour / 23);
  int clippedTime = 0;
  if(tick_time->tm_hour > 20) {
    clippedTime = 960; // (21-5)*60
  }
  if(tick_time->tm_hour > 5) {
    clippedTime = (tick_time->tm_hour - 5)*60 + tick_time->tm_min;
  }
//  int clippedHour = (tick_time->tm_hour < 20) ? tick_time->tm_hour-5 : 15;
//  if(clippedHour<0) clippedHour = 0;
//  int curShould = (g_goal * clippedHour / 15);
  int curShould = (g_goal * clippedTime / 960);
  int curDiff = g_watercnt_ml - curShould;
  int neg = (curDiff<0);
  if(neg) curDiff *= -1;
  
  // Write the current hours and minutes into a buffer
  static char s_buffer[8];
  snprintf(s_buffer,sizeof(s_buffer),"%c%d.%.2d",neg?'-':'+',curDiff/1000,(curDiff/10)%100);
  
  // Display this time on the TextLayer
  text_layer_set_text(s_time_layer, s_buffer);

  static char s_buffer2[16];
  snprintf(s_buffer2,sizeof(s_buffer2),"%d / %d", g_watercnt_ml, g_goal);
  text_layer_set_text(s_subtext,s_buffer2);
//  text_layer_set_text(s_subtext,curDay);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
}

static void main_window_load(Window *window) {
  // Get information about the Window
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Create the TextLayer with specific bounds
  s_time_layer = text_layer_create( GRect(0, 52, bounds.size.w, 50));

  // Improve the layout to be more like a watchface
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorBlack);
//  text_layer_set_text(s_time_layer, "00:00");
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);

  // Add it as a child layer to the Window's root layer
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));

  s_subtext = text_layer_create( GRect(0, 100, bounds.size.w, 30));
  text_layer_set_background_color(s_subtext, GColorClear);
  text_layer_set_text_color(s_subtext, GColorBlack);
  text_layer_set_font(s_subtext, fonts_get_system_font(FONT_KEY_GOTHIC_28));
  text_layer_set_text_alignment(s_subtext, GTextAlignmentCenter);

  layer_add_child(window_layer, text_layer_get_layer(s_subtext));
}

static void main_window_unload(Window *window) {
  // Destroy TextLayer
  text_layer_destroy(s_time_layer);
}

// typedef void(* ClickHandler)(ClickRecognizerRef recognizer, void *context) 
void addADrink(ClickRecognizerRef recognizer, void *context) {
  g_watercnt_ml += 250;
  storeData();
  update_time();
}
void removeADrink(ClickRecognizerRef recognizer, void *context) {
  g_watercnt_ml -= 250;
  if(g_watercnt_ml<0)
    g_watercnt_ml = 0;
  storeData();
  update_time();
}

static void clickConfy(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 0, addADrink);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 0, removeADrink);
}

static void init() {
#if 0 // debug reset ;P
  persist_delete(STORAGE_WATERCNT_ML);
  persist_delete(STORAGE_DAY);
  persist_delete(STORAGE_GOAL_ML);
#endif
  
  if(persist_exists(STORAGE_WATERCNT_ML)) {
    g_watercnt_ml = persist_read_int(STORAGE_WATERCNT_ML);
  }
  if(persist_exists(STORAGE_DAY)) {
    persist_read_string(STORAGE_DAY, g_day, sizeof(g_day));
  }
  if(persist_exists(STORAGE_GOAL_ML)) {
    g_goal = persist_read_int(STORAGE_GOAL_ML);
  }
  
  // Create main Window element and assign to pointer
  s_main_window = window_create();

  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });
  
  window_set_click_config_provider(s_main_window,clickConfy);

  // Show the Window on the watch, with animated=true
  window_stack_push(s_main_window, true);

  update_time();

  // Register with TickTimerService
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void deinit() {
  // Destroy Window
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
