#include "pebble.h"

#define KEY_TEMPERATURE 0
#define KEY_CONDITIONS 7

static GFont s_weather_font;
// POST variables

// Received variables
enum {
	BTC_KEY_CURRENCY = 1,
	BTC_KEY_EXCHANGE = 2,
	BTC_KEY_ASK = 3,
	BTC_KEY_BID = 4,
	BTC_KEY_LAST = 5,
	INVERT_COLOR_KEY = 6
};
	
static Window *window;
static AppTimer *timer;
static AppSync sync;
static uint8_t sync_buffer[124];

static TextLayer *s_weather_layer;
static TextLayer *text_date_layer;
static TextLayer *text_time_layer;
static TextLayer *text_price_layer;
//static TextLayer *text_currency_layer;
//static TextLayer *text_buy_label_layer;
//static TextLayer *text_buy_price_layer;
//static TextLayer *text_sell_label_layer;layer);
//static TextLayer *text_sell_price_layer;

static InverterLayer *inverter_layer = NULL;

static GFont font_last_price_small;
static GFont font_last_price_large;

static bool using_smaller_font = false;

static void set_timer();

//void failed(int32_t cookie, int http_status, void* context) {
//	if(cookie == 0 || cookie == BTC_HTTP_COOKIE) {
//		text_layer_set_text(&text_price_layer, "---");
//	}
//}

static void sync_error_callback(DictionaryResult dict_error, AppMessageResult app_message_error, void *context) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "App Message Sync Error: %d, %d", app_message_error, dict_error);
}

static void set_invert_color(bool invert) {
	if (invert && inverter_layer == NULL) {
		// Add inverter layer
		Layer *window_layer = window_get_root_layer(window);

		inverter_layer = inverter_layer_create(GRect(0, 0, 144, 168));
		layer_add_child(window_layer, inverter_layer_get_layer(inverter_layer));
	} else if (!invert && inverter_layer != NULL) {
		// Remove Inverter layer
		layer_remove_from_parent(inverter_layer_get_layer(inverter_layer));
		inverter_layer_destroy(inverter_layer);
		inverter_layer = NULL;
	}
	// No action required
}

static void sync_tuple_changed_callback(const uint32_t key, const Tuple* new_tuple, const Tuple* old_tuple, void* context) {
	
	static char str[35] = "";
	bool invert;
	switch (key) {
		case BTC_KEY_LAST:
			text_layer_set_text(text_price_layer, new_tuple->value->cstring);
			size_t len = strlen(new_tuple->value->cstring);
			if (len > 6 && !using_smaller_font) {
				text_layer_set_font(text_price_layer, font_last_price_small);
				using_smaller_font = true;
			} else if (len <= 6 && using_smaller_font) {
				text_layer_set_font(text_price_layer, font_last_price_large);
				using_smaller_font = false;
			}
			break;
/*		case BTC_KEY_BID
			text_layer_set_text(text_buy_price_layer, new_tuple->value->cstring);
			break;
		case BTC_KEY_ASK:
			text_layer_set_text(text_sell_price_layer, new_tuple->value->cstring);
			break;
		case BTC_KEY_CURRENCY:
			snprintf(str, sizeof(str), "%s / BTC", new_tuple->value->cstring);
			text_layer_set_text(text_currency_layer, str);
			break;
*/		case INVERT_COLOR_KEY:
			invert = new_tuple->value->uint8 != 0;
			persist_write_bool(INVERT_COLOR_KEY, invert);
			set_invert_color(invert);
			break;
	}
}

static void send_cmd(void) {
	Tuplet value = TupletInteger(0, 1);
	
	DictionaryIterator *iter;
	app_message_outbox_begin(&iter);
	
	if (iter == NULL) {
		return;
	}
	
	dict_write_tuplet(iter, &value);
	dict_write_end(iter);
	
	app_message_outbox_send();
}

static void timer_callback(void *data) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "Timer tick");
	send_cmd();
	set_timer();
}

static void set_timer() {
	// Update again in 60 minutes.
	const uint32_t timeout_ms = 60 * 60 * 1000;
	timer = app_timer_register(timeout_ms, timer_callback, NULL);
}

static void update_time(struct tm *tick_time) {
	// Need to be static because they're used by the system later.
	static char time_text[] = "00:00";
	static char date_text[] = "Xxxxxxxxx 00";
	
	char *time_format;
	
	
	// TODO: Only update the date when it's changed.
	strftime(date_text, sizeof(date_text), "%B %e", tick_time);
	text_layer_set_text(text_date_layer, date_text);
	
	
	if (clock_is_24h_style()) {
		time_format = "%R";
	} else {
		time_format = "%I:%M";
	}
	
	strftime(time_text, sizeof(time_text), time_format, tick_time);
	
	// Kludge to handle lack of non-padded hour format string
	// for twelve hour clock.
	if (!clock_is_24h_style() && (time_text[0] == '0')) {
		memmove(time_text, &time_text[1], sizeof(time_text) - 1);
	}
	
	text_layer_set_text(text_time_layer, time_text);				
}

static void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
	update_time(tick_time);
}

static void window_load(Window *window) {
	Layer *window_layer = window_get_root_layer(window);
	
	font_last_price_small = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DIAVLO_MEDIUM_29));
	font_last_price_large = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DIAVLO_MEDIUM_35));
	
	
	s_weather_layer = text_layer_create(GRect(0, 65, 144, 168));
	text_layer_set_background_color(s_weather_layer, GColorClear);
	text_layer_set_text_color(s_weather_layer, GColorWhite);
	text_layer_set_text_alignment(s_weather_layer, GTextAlignmentCenter);
	text_layer_set_text(s_weather_layer, "Loading...");
	
	s_weather_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DIAVLO_MEDIUM_19));
	text_layer_set_font(s_weather_layer, s_weather_font);
	layer_add_child(window_get_root_layer(window), text_layer_get_layer(s_weather_layer));
						
	text_price_layer = text_layer_create(GRect(0, 0, 144-0, 168-0));
	text_layer_set_text_color(text_price_layer, GColorWhite);
	text_layer_set_background_color(text_price_layer, GColorClear);
	text_layer_set_font(text_price_layer, font_last_price_small);
	text_layer_set_text_alignment(text_price_layer, GTextAlignmentCenter);
	layer_add_child(window_layer, text_layer_get_layer(text_price_layer));
	
/*	text_currency_layer = text_layer_create(GRect(8, 41, 144-8, 168-41));
	text_layer_set_text_color(text_currency_layer, GColorWhite);
	text_layer_set_background_color(text_currency_layer, GColorClear);
	text_layer_set_font(text_currency_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DIAVLO_15)));
	layer_add_child(window_layer, text_layer_get_layer(text_currency_layer));
	
	text_buy_label_layer = text_layer_create(GRect(8, 60, 144-8, 168-60));
	text_layer_set_text_color(text_buy_label_layer, GColorWhite);
	text_layer_set_background_color(text_buy_label_layer, GColorClear);
	text_layer_set_font(text_buy_label_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DIAVLO_15)));
	text_layer_set_text(text_buy_label_layer, "BUY");
	layer_add_child(window_layer, text_layer_get_layer(text_buy_label_layer));
	
	text_buy_price_layer = text_layer_create( GRect(56, 60, 144-56, 168-60));
	text_layer_set_text_color(text_buy_price_layer, GColorWhite);
	text_layer_set_background_color(text_buy_price_layer, GColorClear);
	text_layer_set_font(text_buy_price_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DIAVLO_15)));
	layer_add_child(window_layer, text_layer_get_layer(text_buy_price_layer));
	
	text_sell_label_layer = text_layer_create(GRect(8, 78, 144-8, 168-78));
	text_layer_set_text_color(text_sell_label_layer, GColorWhite);
	text_layer_set_background_color(text_sell_label_layer, GColorClear);
	text_layer_set_font(text_sell_label_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DIAVLO_15)));
	text_layer_set_text(text_sell_label_layer, "SELL");
	layer_add_child(window_layer, text_layer_get_layer(text_sell_label_layer));
	
	text_sell_price_layer = text_layer_create(GRect(56, 78, 144-56, 168-78));
	text_layer_set_text_color(text_sell_price_layer, GColorWhite);
	text_layer_set_background_color(text_sell_price_layer, GColorClear);
	text_layer_set_font(text_sell_price_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DIAVLO_15)));
	layer_add_child(window_layer, text_layer_get_layer(text_sell_price_layer));
*/	
	text_date_layer = text_layer_create(GRect(13, 110, 144-13, 168-110));
	text_layer_set_text_color(text_date_layer, GColorWhite);
	text_layer_set_background_color(text_date_layer, GColorClear);
	text_layer_set_font(text_date_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DIAVLO_MEDIUM_19)));
	layer_add_child(window_layer, text_layer_get_layer(text_date_layer));
	
	text_time_layer = text_layer_create(GRect(15, 125, 144-15, 168-125));
	text_layer_set_text_color(text_time_layer, GColorWhite);
	text_layer_set_background_color(text_time_layer, GColorClear);
	text_layer_set_font(text_time_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DIAVLO_HEAVY_40)));
	layer_add_child(window_layer, text_layer_get_layer(text_time_layer));
	
	tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);
	
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	update_time(t);
	
	Tuplet initial_values[] = {
		TupletCString(BTC_KEY_CURRENCY, "---"),
		TupletCString(BTC_KEY_ASK, "---"),
		TupletCString(BTC_KEY_BID, "---"),
		TupletCString(BTC_KEY_LAST, "---"),
		TupletCString(BTC_KEY_EXCHANGE, "---"),
		TupletInteger(INVERT_COLOR_KEY, persist_read_bool(INVERT_COLOR_KEY)),
	};
	
	app_sync_init(&sync, sync_buffer, sizeof(sync_buffer), initial_values, ARRAY_LENGTH(initial_values),
				  sync_tuple_changed_callback, sync_error_callback, NULL);
	
	//send_cmd();
	timer = app_timer_register(2000, timer_callback, NULL);
	//set_timer();
		if(t->tm_min % 30 == 0) {
    // Begin dictionary
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);

    // Add a key-value pair
    dict_write_uint8(iter, 0, 0);

    // Send the message!
    app_message_outbox_send();
};
}


static void window_unload(Window *window) {
	app_sync_deinit(&sync);
	
	tick_timer_service_unsubscribe();
	
	text_layer_destroy(text_date_layer);
	text_layer_destroy(text_time_layer);
	text_layer_destroy(text_price_layer);
	text_layer_destroy(s_weather_layer);
/*	text_layer_destroy(text_currency_layer);
	text_layer_destroy(text_buy_label_layer);
	text_layer_destroy(text_buy_price_layer);
	text_layer_destroy(text_sell_label_layer);
	text_layer_destroy(text_sell_price_layer); */
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  // Store incoming information
  static char temperature_buffer[8];
  static char conditions_buffer[32];
  static char weather_layer_buffer[64];
  // Read first item
  Tuple *t = dict_read_first(iterator);

  // For all items
  while(t != NULL) {
    // Which key was received?
    switch(t->key) {
    case KEY_TEMPERATURE:
		snprintf(temperature_buffer, sizeof(temperature_buffer), "%dC", (int)t->value->int32);
      break;
    case KEY_CONDITIONS:
		snprintf(conditions_buffer, sizeof(conditions_buffer), "%s", t->value->cstring);
      break;
    default:
      APP_LOG(APP_LOG_LEVEL_ERROR, "Key %d not recognized!", (int)t->key);
      break;
    }

    // Look for next item
    t = dict_read_next(iterator);
  }
	// Assemble full string and display
	snprintf(weather_layer_buffer, sizeof(weather_layer_buffer), "%s, %s", temperature_buffer, conditions_buffer);
	text_layer_set_text(s_weather_layer, weather_layer_buffer);
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

static void init(void) {
	window = window_create();
	window_set_background_color(window, GColorBlack);
	window_set_fullscreen(window, true);
	window_set_window_handlers(window, (WindowHandlers) {
		.load = window_load,
		.unload = window_unload
	});
	
	const int inbound_size = 124;
	const int outbound_size = 124;
	app_message_open(inbound_size, outbound_size);
	
	const bool animated = true;
	window_stack_push(window, animated);
	
	// Register callbacks
	app_message_register_inbox_received(inbox_received_callback);
	app_message_register_inbox_dropped(inbox_dropped_callback);
	app_message_register_outbox_failed(outbox_failed_callback);
	app_message_register_outbox_sent(outbox_sent_callback);
	
	// Open AppMessage
	app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
}

static void deinit(void) {
	window_destroy(window);
}

int main(void){
	init();
	app_event_loop();
	deinit();
}