#include <pebble.h>

enum {
  STORAGE_WATERCNT_ML =23, // <-- no longer used, replaced by STORAGE_DRINKS
  STORAGE_DAY,
  STORAGE_GOAL_ML,
  STORAGE_DRINKS,
};

#define DEFAULT_GOAL 5000

typedef enum {
  DT_WATER_250ML =0,
  DT_WATER_100ML =1,
  DT_WATER_333ML =2,
  DT_WATER_200ML =3,

//  DT_COFFEE_100ML=4,

  // beer? 250ML?
  // ...
  MAX_DRINK_TYPES = 3,
} DrinkType;

static int g_activeDrinkType = DT_WATER_250ML;

static int g_goal = DEFAULT_GOAL;
static char g_day[PERSIST_STRING_MAX_LENGTH] = { '\0' };
/*data structure:
- afaik it is possible to store an array of 16bit-int
... to be correct it allows storing 256byte "void*", so 128 16bit-integers
- so storing time in e.g. 11bit (minutes per day 1440) ... and double-bit and and 16types
- 
*/
// storage API supports 256byte --> 128 uint16
#define MAX_DRINKS 128
static uint16_t g_drinks[MAX_DRINKS] = {};

static void storeData() {
  persist_write_int(STORAGE_GOAL_ML, g_goal);
  persist_write_string(STORAGE_DAY, g_day);
  persist_write_data(STORAGE_DRINKS, g_drinks, sizeof(g_drinks));
}

static void loadData() {
  if(persist_exists(STORAGE_DAY)) {
    persist_read_string(STORAGE_DAY, g_day, sizeof(g_day));
  }
  if(persist_exists(STORAGE_GOAL_ML)) {
    g_goal = persist_read_int(STORAGE_GOAL_ML);
  }
  if(persist_exists(STORAGE_DRINKS)) {
    persist_read_data(STORAGE_DRINKS, g_drinks, sizeof(g_drinks));
  }
}

static void clearData() {
  persist_delete(STORAGE_WATERCNT_ML);
  persist_delete(STORAGE_DAY);
  persist_delete(STORAGE_GOAL_ML);
  persist_delete(STORAGE_DRINKS);
}

int numDrinks() {
  for(int i=0 ; i<MAX_DRINKS ; ++i) {
    if(g_drinks[i] == 0) {
      return i;
    }
  }
  return MAX_DRINKS;
}


// 1 bit doubling
// 4 bit drink type
// 11 bit time
int drinkTime(int i) {
  return (g_drinks[i] & 0x7ff);
}
int drinkType(int i) {
  return ((g_drinks[i] >> 11) & 0xf);
}
int drinkFactor(int i) {
  return ((g_drinks[i] & 0x8000) ? 2 : 1);
}
int drinkVolume(int i) {
  const int f = drinkFactor(i);

  switch(drinkType(i)) {
  case DT_WATER_250ML:
    return 250*f;
  case DT_WATER_100ML:
    return 100*f;
  case DT_WATER_333ML:
    return 333*f;
  case DT_WATER_200ML:
    return 200*f;
//  case DT_COFFEE_100ML:
//    return 100*f;
  default:
    return 0;
  }
}

int calcDrinksVolume() {
  int sum = 0;
  for(int i=0 ; i<MAX_DRINKS ; ++i) {
    if(g_drinks[i] == 0) {
      break;
    }

    sum += drinkVolume(i);
  }
  return sum;
}

void setDrink(int idx, int time, int type) {
  g_drinks[idx] = (type << 11) | time;
}

bool tryIncreaseDrinkFactor(int idx) {
  if(!(g_drinks[idx] & 0x8000)) {
    g_drinks[idx] |= 0x8000;
    return true;
  }
  return false;
}

bool tryDecreaseDrinkFactor(int idx) {
  if(g_drinks[idx] & 0x8000) {
    g_drinks[idx] &= 0x7fff;
    return true;
  }
  return false;
}

int curMins() {
  time_t curT = time(NULL);
  struct tm* curTM = localtime(&curT);

  return (curTM->tm_hour * 60 + curTM->tm_min);
}

static const int DOUBLE_INTERVAL = 2; //mins
void addDrink(int type) {
  const int num = numDrinks();
  const int t = curMins();  

  if(num >= MAX_DRINKS) {
    return;
  }

  if(num > 0 && drinkType(num-1) == type
     && drinkTime(num-1) + DOUBLE_INTERVAL >= t)
  {
    if(!tryIncreaseDrinkFactor(num-1)) {
      setDrink(num,t,type);
    }
  }
  else {
    setDrink(num,t,type);
  }
}

void removeDrink() {
  const int num = numDrinks();

  if(num > 0) {
    if(!tryDecreaseDrinkFactor(num-1)) {
      g_drinks[num-1] = 0;
    }
  }
}

static Window *s_main_window; 
static TextLayer *s_time_layer;
static TextLayer *s_subtext;
static TextLayer *s_drinkLbl;

static void update_time() {
  // Get a tm structure
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  int waterCntMl = calcDrinksVolume();
  
  static char curDay[8];
  strftime(curDay, sizeof(curDay), "%m-%d", tick_time); // %F = "%Y-%m-%d" ... but the year doesn't matter (unless you don't use the app for exactly one year °°)
  if(0 != strcmp(curDay,g_day)) {
    if(waterCntMl > DEFAULT_GOAL/2) {
      g_goal = waterCntMl;
    }
    waterCntMl = 0;
    memset(g_drinks,0,sizeof(g_drinks));
    strcpy(g_day,curDay);
    storeData();
  }

//  int curShould = (g_goal * tick_time->tm_hour / 23);
  int clippedTime = 0;
  if(tick_time->tm_hour > 20) {
    clippedTime = 960; // (21-5)*60
  }
  else if(tick_time->tm_hour > 5) {
    clippedTime = (tick_time->tm_hour - 5)*60 + tick_time->tm_min;
  }
//  int clippedHour = (tick_time->tm_hour < 20) ? tick_time->tm_hour-5 : 15;
//  if(clippedHour<0) clippedHour = 0;
//  int curShould = (g_goal * clippedHour / 15);
  int curShould = (g_goal * clippedTime / 960);
  int curDiff = waterCntMl - curShould;
  int neg = (curDiff<0);
  if(neg) curDiff *= -1;
  
  // Write the current hours and minutes into a buffer
  static char s_buffer[8];
  snprintf(s_buffer,sizeof(s_buffer),"%c%d.%.2d",neg?'-':'+',curDiff/1000,(curDiff/10)%100);
  
  // Display this time on the TextLayer
  text_layer_set_text(s_time_layer, s_buffer);

  static char s_buffer2[16];
  snprintf(s_buffer2,sizeof(s_buffer2),"%d / %d", waterCntMl, g_goal);
  text_layer_set_text(s_subtext,s_buffer2);
//  text_layer_set_text(s_subtext,curDay);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
}

static Layer* g_layer = 0;
#define MAX_WATER_PATH_POINTS 16
static GPoint g_water_path_points[MAX_WATER_PATH_POINTS] = {};
static GPath* g_water_path = NULL;
static const int lineWidth = 3;

static void update_layer(struct Layer* layer, GContext* ctx) {
  if(!g_water_path) {
    GPathInfo pInf = (GPathInfo){ .num_points = MAX_WATER_PATH_POINTS };
    pInf.points = g_water_path_points;
    g_water_path = gpath_create(&pInf);
  }

  GRect bounds = layer_get_bounds(g_layer);

  int y=bounds.size.h;
  g_water_path_points[0] = (GPoint){ .x = 0 , .y = y + lineWidth };
  int ct = curMins();
  int ip = 1;
  const int futureMins = PBL_IF_RECT_ELSE(10, 30);
  const int numD = numDrinks();
  const int minsToPx = 1;
  for(int i=0 ; i<numD ; ++i) {
    const int dt = drinkTime(i);
    // as the display is roughly 150px width, using minutes 1:1 fits roughly 2 hours on screen
    const int x = bounds.size.w+(dt-ct-futureMins)*minsToPx;
    if(x<0) continue;
    y -= drinkVolume(i)/10;
    if(ip>=(MAX_WATER_PATH_POINTS-1)) continue; //FIXME: maybe better do this inverse ;)
    g_water_path_points[ip++] = (GPoint){ .x = x , .y = y };
  }
  for(;ip<(MAX_WATER_PATH_POINTS-1);++ip) {
    g_water_path_points[ip] = (GPoint){ .x = bounds.size.w + lineWidth , .y = y };
  }
  g_water_path_points[(MAX_WATER_PATH_POINTS-1)].x = bounds.size.w + lineWidth;
  g_water_path_points[(MAX_WATER_PATH_POINTS-1)].y = bounds.size.h + lineWidth;

//note: antialiased is default on, but it seems the emulator has no proper handling?
//  graphics_context_set_antialiased(ctx, true);

  // Fill the path:
  graphics_context_set_fill_color(ctx, GColorFromRGB(170, 170, 255));
  gpath_draw_filled(ctx, g_water_path);

  // Stroke the path:
#ifdef PBL_COLOR
  graphics_context_set_stroke_color(ctx, GColorBlue);
#else
  graphics_context_set_stroke_color(ctx, GColorBlack);
#endif
  graphics_context_set_stroke_width(ctx, lineWidth); //note: only odd width supported (1,3,5,...)
  gpath_draw_outline(ctx, g_water_path);
}

static void main_window_load(Window *window) {
  // Get information about the Window
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  g_layer = layer_create(bounds);
  layer_set_update_proc(g_layer, update_layer);
  layer_add_child(window_layer, g_layer);

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

  //NOTE: gothic font <28: 24,18,14
  s_drinkLbl = text_layer_create( GRect(0, 130, bounds.size.w, 24));
  text_layer_set_background_color(s_drinkLbl, GColorClear);
  text_layer_set_text_color(s_drinkLbl, GColorFromRGB(100, 100, 100));
  text_layer_set_font(s_drinkLbl, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_drinkLbl, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_drinkLbl));

  text_layer_set_text(s_drinkLbl,"Water 250ML");

  StatusBarLayer* status_bar = status_bar_layer_create();
  layer_add_child(window_layer, status_bar_layer_get_layer(status_bar));
  status_bar_layer_set_separator_mode(status_bar, StatusBarLayerSeparatorModeDotted);
  status_bar_layer_set_colors(status_bar, GColorClear, GColorBlack);
}

static void main_window_unload(Window *window) {
  // Destroy TextLayer
  text_layer_destroy(s_time_layer);
}

// typedef void(* ClickHandler)(ClickRecognizerRef recognizer, void *context) 
void addADrink(ClickRecognizerRef recognizer, void *context) {
  addDrink(g_activeDrinkType);
  storeData();
  update_time();
}
void removeADrink(ClickRecognizerRef recognizer, void *context) {
  removeDrink();
  storeData();
  update_time();
}
void changeDrinkType(ClickRecognizerRef recognizer, void *context) {
  if(++g_activeDrinkType > MAX_DRINK_TYPES) {
    g_activeDrinkType = 0;
  }

  switch(g_activeDrinkType) {
  case DT_WATER_250ML:
    text_layer_set_text(s_drinkLbl,"Water 250ML");
    break;
  case DT_WATER_100ML:
    text_layer_set_text(s_drinkLbl,"Water 100ML");
    break;
  case DT_WATER_333ML:
    text_layer_set_text(s_drinkLbl,"Water 333ML");
    break;
  case DT_WATER_200ML:
    text_layer_set_text(s_drinkLbl,"Water 200ML");
    break;
//  case DT_COFFEE_100ML:
//    text_layer_set_text(s_drinkLbl,"Coffee 100ML");
//    break;
  default:
    text_layer_set_text(s_drinkLbl,"? 0ML");
    break;
  }
}

static void clickConfy(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 0, addADrink);
  window_single_repeating_click_subscribe(BUTTON_ID_SELECT, 0, removeADrink);
//  window_long_click_subscribe(BUTTON_ID_UP, 1000, removeADrink, NULL); // <-- FIXME this would first trigger the single click (addADrink) before firing the one -.-
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 0, changeDrinkType);
}

static void init() {
#if 0 // debug reset ;P
  clearData();
#endif
  
  loadData();

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
