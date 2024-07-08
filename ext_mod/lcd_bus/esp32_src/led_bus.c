
typedef struct {
    rmt_tx_done_callback_t on_trans_done; /*!< Event callback, invoked when transmission is finished */
} rmt_tx_event_callbacks_t;


typedef struct {
    int loop_count; /*!< Specify the times of transmission in a loop, -1 means transmitting in an infinite loop */
    struct {
        uint32_t eot_level : 1;         /*!< Set the output level for the "End Of Transmission" */
        uint32_t queue_nonblocking : 1; /*!< If set, when the transaction queue is full, driver will not block the thread but return directly */
    } flags;                            /*!< Transmit specific config flags */
} rmt_transmit_config_t;


rmt_sync_manager_config_t
tx_channel_array
array_size


rmt_new_tx_channel(const rmt_tx_channel_config_t *config, rmt_channel_handle_t *ret_chan)
rmt_transmit(rmt_channel_handle_t tx_channel, rmt_encoder_handle_t encoder, const void *payload, size_t payload_bytes, const rmt_transmit_config_t *config)
rmt_tx_register_event_callbacks(rmt_channel_handle_t tx_channel, const rmt_tx_event_callbacks_t *cbs, void *user_data)
rmt_tx_wait_all_done(rmt_channel_handle_t tx_channel, int timeout_ms)


static bool led_rmt_bus_trans_done_cb(rmt_channel_handle_t tx_chan, const rmt_tx_done_event_data_t *edata, void *user_ctx)
{
    mp_lcd_led_bus_obj_t *self = (mp_lcd_led_bus_obj_t *)user_ctx;
    LCD_UNUSED(tx_chan);
    LCD_UNUSED(edata);

    if (self->callback != mp_const_none && mp_obj_is_callable(self->callback)) {
        cb_isr(self->callback);
    }
    self->trans_done = true;
    return false;
}


#include "driver/rmt_common.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_types.h"


#define START_HEADER_SIZE  4

void c_temp2rgb(uint16_t kelvin, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint16_t temp = kelvin / 100;

    if (temp <= 66 ) {
        *r = 255;
        *g = (uint8_t)(99.4708025861f * log(temp) - 161.1195681661f);
        if (temp <= 19) {
            *b = 0;
        } else {
            *b = (uint8_t)(138.5177312231f * log(temp - 10) - 305.0447927307f);
        }
    } else {
        *r = (uint8_t)(329.698727446f * powf((float)(temp - 60), -0.1332047592f));
        *g = (uint8_t)(288.1221695283f * powf((float)(temp - 60), -0.0755148492f));
        *b = 255;
    }
}

#define LED_MIN(x, y) x < y ? x : y

// The  RGB to RGBW conversion function.
void rgb2rgbw(mp_lcd_led_color_temp *color_temp, uint8_t rgbw[])
{
    // Calculate all of the color's white values corrected taking into account the white color temperature.
    float wRed = (float)(rgbw[0]) * (255.0f / (float)color_temp->r);
    float wGreen = (float)(rgbw[1]) * (255.0f / (float)color_temp->g);
    float wBlue = (float)(rgbw[2]) * (255.0f / (float)color_temp->b);
    
    // Determine the smallest white value from above.
    uint8_t wMin = roundf(LED_MIN(wRed, LED_MIN(wGreen, wBlue)));
    
    // Make the color with the smallest white value to be the output white value
    if (wMin == wRed) {
        rgbw[3] = rgbw[0];
    } else if (wMin == wGreen) {
        rgbw[3] = rgbw[1];
    } else {
        rgbw[3] = rgbw[2];
    }

    rgbw[0] = round(rgbw[0] - rgbw[3] * (color_temp->r / 255));
    rgbw[1] = round(rgbw[1] - rgbw[3] * (color_temp->g / 255));
    rgbw[2] = round(rgbw[2] - rgbw[3] * (color_temp->b / 255));

    if (color_temp->blue_correct) *w = (*w) - (*b) * 0.2;
}


void led_spi_deinit_callback(machine_hw_spi_device_obj_t *device);


mp_lcd_err_t led_del(mp_obj_t obj);
mp_lcd_err_t led_init(mp_obj_t obj, uint16_t width, uint16_t height, uint8_t bpp, uint32_t buffer_size, bool rgb565_byte_swap, uint8_t cmd_bits, uint8_t param_bits);
mp_lcd_err_t led_get_lane_count(mp_obj_t obj, uint8_t *lane_count);
mp_lcd_err_t led_rx_param(mp_obj_t obj, int lcd_cmd, void *param, size_t param_size);
mp_lcd_err_t led_tx_param(mp_obj_t obj, int lcd_cmd, void *param, size_t param_size);
mp_lcd_err_t led_tx_color(mp_obj_t obj, int lcd_cmd, void *color, size_t color_size, int x_start, int y_start, int x_end, int y_end);
mp_obj_t led_allocate_framebuffer(mp_obj_t obj, uint32_t size, uint32_t caps);
mp_obj_t led_free_framebuffer(mp_obj_t obj, mp_obj_t buf);


static size_t led_encode_strip(rmt_encoder_t *encoder, rmt_channel_handle_t channel, const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_handle_t bytes_encoder = led_encoder->bytes_encoder;
    rmt_encoder_handle_t copy_encoder = led_encoder->copy_encoder;
    rmt_encode_state_t session_state = 0;
    rmt_encode_state_t state = 0;
    size_t encoded_symbols = 0;
    switch (led_encoder->state) {
        case 0: // send RGB data
            encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size, &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                led_encoder->state = 1; // switch to next state when current encoding session finished
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {
                state |= RMT_ENCODING_MEM_FULL;
                goto out; // yield if there's no free space for encoding artifacts
            }
        // fall-through
        case 1: // send reset code
            encoded_symbols += copy_encoder->encode(copy_encoder, channel, &led_encoder->reset_code,
                                                    sizeof(led_encoder->reset_code), &session_state);
            if (session_state & RMT_ENCODING_COMPLETE) {
                led_encoder->state = 0; // back to the initial encoding session
                state |= RMT_ENCODING_COMPLETE;
            }
            if (session_state & RMT_ENCODING_MEM_FULL) {
                state |= RMT_ENCODING_MEM_FULL;
                goto out; // yield if there's no free space for encoding artifacts
            }
    }
out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t led_del_strip(rmt_encoder_t *encoder)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_del_encoder(led_encoder->bytes_encoder);
    rmt_del_encoder(led_encoder->copy_encoder);
    free(led_encoder);
    return ESP_OK;
}

static esp_err_t led_reset_strip(rmt_encoder_t *encoder)
{
    rmt_led_strip_encoder_t *led_encoder = __containerof(encoder, rmt_led_strip_encoder_t, base);
    rmt_encoder_reset(led_encoder->bytes_encoder);
    rmt_encoder_reset(led_encoder->copy_encoder);
    led_encoder->state = 0;
    return ESP_OK;
}
    
    
static mp_obj_t mp_lcd_led_bus_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args)
{
    enum {
        ARG_data_pin,
        ARG_freq,
        ARG_ic_type,
        ARG_byte_order,
        ARG_leds_per_pixel,
        ARG_white_color_temp;
        ARG_blue_correct;
        ARG_high0,
        ARG_low0,
        ARG_high1,
        ARG_low1,
        ARG_res,
        ARG_spi_bus
    };

    const mp_arg_t make_new_args[] = {
        { MP_QSTR_data_pin,         MP_ARG_INT  | MP_ARG_KW_ONLY, {.u_int = -1            } },
        { MP_QSTR_freq,             MP_ARG_INT  | MP_ARG_KW_ONLY, {.u_int = 10000000      } },
        { MP_QSTR_ic_type,          MP_ARG_INT  | MP_ARG_KW_ONLY, {.u_int = CUSTOM        } },
        { MP_QSTR_byte_order,       MP_ARG_INT  | MP_ARG_KW_ONLY, {.u_int = GRB           } },
        { MP_QSTR_leds_per_pixel,   MP_ARG_INT  | MP_ARG_KW_ONLY, {.u_int = 3             } },
        { MP_QSTR_white_color_temp, MP_ARG_INT  | MP_ARG_KW_ONLY, {.u_int = 3000          } },
        { MP_QSTR_blue_correct,     MP_ARG_BOOL | MP_ARG_KW_ONLY, {.u_bool = false        } },
        { MP_QSTR_high0,            MP_ARG_INT  | MP_ARG_KW_ONLY, {.u_int = -1            } },
        { MP_QSTR_low0,             MP_ARG_INT  | MP_ARG_KW_ONLY, {.u_int = -1            } },
        { MP_QSTR_high1,            MP_ARG_INT  | MP_ARG_KW_ONLY, {.u_int = -1            } },
        { MP_QSTR_low1,             MP_ARG_INT  | MP_ARG_KW_ONLY, {.u_int = -1            } },
        { MP_QSTR_res,              MP_ARG_INT  | MP_ARG_KW_ONLY, {.u_int = -1            } },
        { MP_QSTR_spi_bus,          MP_ARG_OBJ  | MP_ARG_KW_ONLY, {.u_obj = mp_const_none } },

    };

    mp_arg_val_t args[MP_ARRAY_SIZE(make_new_args)];
    mp_arg_parse_all_kw_array(
        n_args,
        n_kw,
        all_args,
        MP_ARRAY_SIZE(make_new_args),
        make_new_args,
        args
    );

    // create new object
    mp_lcd_led_bus_obj_t *self = m_new_obj(mp_lcd_led_bus_obj_t);
    self->base.type = &mp_lcd_led_bus_type;

    self->callback = mp_const_none;

    self->leds_per_pixel = (uint8_t)args[ARG_leds_per_pixel].u_int;
    self->color_temp.blue_correct = (bool)args[ARG_blue_correct].u_bool;

    if (self->leds_per_pixel == 4) {
        uint16_t color_temp = (uint16_t)args[ARG_white_color_temp].u_int;
        if (color_temp == 0) color_temp = 3000;
        c_temp2rgb(color_temp, &self->color_temp.r, &self->color_temp.g, &self->color_temp.b);
    }
    
    mp_lcd_led_pixel_order byte_order = (mp_lcd_led_pixel_order)args[ARG_byte_order].u_int;

    if (args[ARG_spi_bus].u_int == mp_const_none) {
        self->data_pin = (esp_gpio_t)args[ARG_data_pin].u_int;

        self->strip_encoder = m_new_obj(mp_lcd_led_strip_encoder_t);
        mp_lcd_led_ic_type ic_type = (mp_lcd_led_ic_type)args[ARG_ic_type].u_int;


        switch(ic_type) {
            case APA105:
            case APA109:
            case SK6805:
            case SK6812:
            case SK6818:
            case WS2813:
                self->bit0.high = 300;
                break;
            case APA104:
            case SK6822:
            case WS2812:
                self->bit0.high = 350;
                break;
            case WS2818A:
            case WS2818B:
            case WS2851:
            case WS2815B:
            case WS2815:
            case WS2811:
            case WS2814:
            case WS2818:
                self->bit0.high = 220;
                break;
            case WS2816A:
            case WS2816B:
            case WS2816C:
                self->bit0.high = 200;
                break;
            case WS2812B:
                self->bit0.high = 400;
                break;
            case SK6813:
                self->bit0.high = 240;
                break;
            case CUSTOM:
                if (args[ARG_high0].u_int == -1) {
                    mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("You must supply high0 parameter"));
                    return mp_const_none;
                }
                self->bit0.high = (uint16_t)args[ARG_high0].u_int;
                break;

            default:
                mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Unsupported IC type"));
                return mp_const_none;
        }

        switch(ic_type) {
            case APA105:
            case APA109:
            case SK6805:
            case SK6812:
            case SK6818:
                self->bit1.high = 600;
                break;
            case WS2818A:
            case WS2818B:
            case WS2851:
            case WS2815B:
            case WS2815:
            case WS2811:
            case WS2814:
                self->bit1.high = 580;
                break;
            case WS2816A:
            case WS2816B:
            case WS2816C:
                self->bit1.high = 520;
                break;
            case WS2818:
            case WS2813:
                self->bit1.high = 750;
                break;
            case WS2812B:
                self->bit1.high = 800;
                break;
            case WS2812:
                self->bit1.high = 700;
                break;
            case APA104:
            case SK6822:
                self->bit1.high = 1360;
                break;
            case SK6813:
                self->bit1.high = 740;
                break;

            case CUSTOM:
                if (args[ARG_high1].u_int == -1) {
                    mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("You must supply high1 parameter"));
                    return mp_const_none;
                }
                self->bit1.high = (uint16_t)args[ARG_high1].u_int;
                break;

            default:
                mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Unsupported IC type"));
                return mp_const_none;
        }

        switch(ic_type) {
            case APA105:
            case APA109:
            case SK6805:
            case SK6812:
            case SK6818:
                self->bit0.low = 900;
                break;
            case APA104:
            case SK6822:
                self->bit0.low = 1360;
                break;
            case SK6813:
            case WS2812:
            case WS2816A:
            case WS2816B:
            case WS2816C:
                self->bit0.low = 800;
                break;
            case WS2812B:
                self->bit0.low = 850;
                break;
            case WS2813:
                self->bit0.low = 300;
                break;
            case WS2818:
                self->bit0.low = 750;
                break;
            case WS2818A:
            case WS2818B:
            case WS2851:
            case WS2815B:
            case WS2815:
            case WS2811:
            case WS2814:
                self->bit0.low = 580;
                break;

            case CUSTOM:
                if (args[ARG_low0].u_int == -1) {
                    mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("You must supply low0 parameter"));
                    return mp_const_none;
                }
                self->bit0.low = (uint16_t)args[ARG_low0].u_int;
                break;

            default:
                mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Unsupported IC type"));
                return mp_const_none;

        }

        switch(ic_type) {
            case APA105:
            case APA109:
            case SK6805:
            case SK6812:
            case SK6818:
            case WS2812:
                self->bit1.low = 600;
                break;
            case APA104:
            case SK6822:
                self->bit1.low = 350;
                break;
            case SK6813:
                self->bit1.low = 200;
                break;
            case WS2812B:
                self->bit1.low = 450;
                break;
            case WS2813:
                self->bit1.low = 300;
                break;
            case WS2818:
            case WS2818A:
            case WS2818B:
            case WS2851:
            case WS2815B:
            case WS2815:
            case WS2811:
                self->bit1.low = 220;
                break;
            case WS2816A:
            case WS2816B:
            case WS2816C:
                self->bit1.low = 480;
                break;
            case WS2814:
                self->bit1.low = 580;
                break;

            case CUSTOM:
                if (args[ARG_low1].u_int == -1) {
                    mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("You must supply low1 parameter"));
                    return mp_const_none;
                }
                self->bit1.low = (uint16_t)args[ARG_low1].u_int;
                break;

            default:
                mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Unsupported IC type"));
                return mp_const_none;

        }

        switch(ic_type) {
            case APA105:
            case APA109:
            case SK6805:
            case SK6812:
            case SK6818:
            case SK6813:
                self->res = 800;
                break;
            case APA104:
                self->res = 240;
                break;
            case SK6822:
                self->res = 500;
                break;
            case WS2812:
            case WS2812B:
                self->res = 5000;
                break;
            case WS2813:
            case WS2818:
                self->res = 300;
                break;
            case WS2816A:
            case WS2816B:
            case WS2816C:
            case WS2818A:
            case WS2818B:
            case WS2851:
            case WS2815B:
            case WS2815:
            case WS2811:
            case WS2814:
                self->res = 280;
                break;

            case CUSTOM:
                if (args[ARG_res].u_int == -1) {
                    mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("You must supply res parameter"));
                    return mp_const_none;
                }
                self->res = (uint16_t)args[ARG_res].u_int;
                break;

            default:
                mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Unsupported IC type"));
                return mp_const_none;
        }

        switch(ic_type) {
            case WS2818A:
            case WS2818B:
            case WS2851:
            case WS2818:
            case SK6822:
            case SK6818:
            case APA104:
            case WS2814:
                byte_order = RGB;
                break;
            case WS2815B:
            case WS2815:
            case WS2811:
            case WS2816A:
            case WS2816B:
            case WS2816C:
            case WS2813:
            case WS2812:
            case WS2812B:
            case SK6813:
            case APA105:
            case APA109:
            case SK6805:
            case SK6812:
                byte_order = GRB;
                break;
            case CUSTOM:
                break;
            default:
                mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Unsupported IC type"));
                return mp_const_none;
        }
    } else {
        machine_hw_spi_bus_obj_t *spi_bus = MP_OBJ_TO_PTR(args[ARG_spi_bus].u_obj);

        self->panel_io_handle.panel_io = NULL;

        self->bus_handle = m_new_obj(esp_lcd_spi_bus_handle_t);

        self->panel_io_config = m_new_obj(esp_lcd_panel_io_spi_config_t);
        self->panel_io_config->cs_gpio_num = -1;
        self->panel_io_config->dc_gpio_num = -1;
        self->panel_io_config->spi_mode = 0;
        self->panel_io_config->pclk_hz = (unsigned int)args[ARG_freq].u_int;
        self->panel_io_config->on_color_trans_done = &bus_trans_done_cb;
        self->panel_io_config->user_ctx = self;
        self->panel_io_config->flags.lsb_first = 0;
        self->panel_io_config->trans_queue_depth = 10;
        self->panel_io_config->lcd_cmd_bits = 8;
        self->panel_io_config->lcd_param_bits = 8;

        self->spi_device = m_new_obj(machine_hw_spi_device_obj_t);
        self->spi_device->active = true;
        self->spi_device->base.type = &machine_hw_spi_device_type;
        self->spi_device->spi_bus = spi_bus;
        self->spi_device->deinit = &led_spi_deinit_callback;
        self->spi_device->user_data = self;
    }

    self->freq = (uint32_t)args[ARG_freq].u_int;
    
    if (byte_order < 0 || byte_order > BGR) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Invalid byte order"));
        return mp_const_none;
    }
                
    switch(byte_order) {
        case RGB:
        case RBG:
            self->rgb_order[0] = 0;
            break;
        case GRB:
        case GBR
            self->rgb_order[0] = 1;
            break;
        case BRG:
        case BGR:
            self->rgb_order[0] = 2;
            break;                        
    }
        
    switch(byte_order) {
        case GRB:
        case BRG:
            self->rgb_order[1] = 0;
            break;
        case RGB:
        case BGR:
            self->rgb_order[1] = 1;
            break;
        case RBG:
        case GBR:
            self->rgb_order[1] = 2;
            break;
    }
        
    switch(byte_order) {
        case GBR:
        case BGR:
            self->rgb_order[2] = 0;
            break;
        case RBG:
        case BRG:
            self->rgb_order[2] = 1;
            break;
        case RGB:
        case GRB:
            self->rgb_order[2] = 2;
            break;
    }
    self->pixel_order = byte_order;

    self->panel_io_handle.get_lane_count = &led_get_lane_count;
    self->panel_io_handle.del = &led_del;
    self->panel_io_handle.rx_param = &led_rx_param;
    self->panel_io_handle.tx_param = &led_tx_param;
    self->panel_io_handle.tx_color = &led_tx_color;
    self->panel_io_handle.allocate_framebuffer = &led_allocate_framebuffer;
    self->panel_io_handle.free_framebuffer = &led_free_framebuffer;
    self->panel_io_handle.init = &led_init;

    return MP_OBJ_FROM_PTR(self);
}

mp_lcd_err_t led_del(mp_obj_t obj)
{
    // mp_lcd_led_bus_obj_t *self = (mp_lcd_led_bus_obj_t *)obj;
    LCD_UNUSED(obj);
    return LCD_OK;
}

mp_lcd_err_t led_rx_param(mp_obj_t obj, int lcd_cmd, void *param, size_t param_size)
{
    LCD_UNUSED(obj);
    LCD_UNUSED(param);
    LCD_UNUSED(lcd_cmd);
    LCD_UNUSED(param_size);

    return LCD_OK;
}

mp_lcd_err_t led_tx_param(mp_obj_t obj, int lcd_cmd, void *param, size_t param_size)
{
    LCD_UNUSED(obj);
    LCD_UNUSED(param);
    LCD_UNUSED(lcd_cmd);
    LCD_UNUSED(param_size);

    return LCD_OK;
}

mp_obj_t led_free_framebuffer(mp_obj_t obj, mp_obj_t buf)
{
    mp_lcd_led_bus_obj_t *self = (mp_lcd_led_bus_obj_t *)obj;

    if (self->panel_handle != NULL) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Unable to free buffer"));
        return mp_const_none;
    }

    mp_obj_array_t *array_buf = (mp_obj_array_t *)MP_OBJ_TO_PTR(buf);
    void *item_buf = array_buf->items;

    if (array_buf == self->view1) {
        heap_caps_free(item_buf);
        self->view1 = NULL;
    } else if (array_buf == self->view2) {
        heap_caps_free(item_buf);
        self->view2 = NULL;
    } else {
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("No matching buffer found"));
    }
    return mp_const_none;
}

mp_obj_t led_allocate_framebuffer(mp_obj_t obj, uint32_t size, uint32_t caps)
{
    mp_lcd_led_bus_obj_t *self = (mp_lcd_led_bus_obj_t *)obj;

    void *buf = heap_caps_calloc(1, 1, MALLOC_CAP_INTERNAL);
    mp_obj_array_t *view = MP_OBJ_TO_PTR(mp_obj_new_memoryview(BYTEARRAY_TYPECODE, 1, buf));
    view->typecode |= 0x80; // used to indicate writable buffer

    if ((caps | MALLOC_CAP_SPIRAM) == caps) {
        uint32_t available = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
        if (available < size) {
            heap_caps_free(buf);
            mp_raise_msg_varg(
                &mp_type_MemoryError,
                MP_ERROR_TEXT("Not enough memory available in SPIRAM (%d)"),
                size
            );
            return mp_const_none;
        }
        self->panel_io_config.flags.fb_in_psram = 1;

        if (self->view1 == NULL) {
            self->buffer_size = size;
            self->view1 = view;
        } else if (self->buffer_size != size) {
            heap_caps_free(buf);
            mp_raise_msg_varg(
                &mp_type_MemoryError,
                MP_ERROR_TEXT("Frame buffer sizes do not match (%d)"),
                size
            );
            return mp_const_none;
        } else if (self->view2 == NULL) {
            self->view2 = view;
            self->panel_io_config.flags.double_fb = 1;
        } else {
            heap_caps_free(buf);
            mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("There is a maximum of 2 frame buffers allowed"));
            return mp_const_none;
        }

        return MP_OBJ_FROM_PTR(view);
    } else {
        uint32_t available = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (size % 2 != 0) {
            heap_caps_free(buf);
            mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("bounce buffer size needs to be divisible by 2"));
            return mp_const_none;
        }

        if (available < size) {
            heap_caps_free(buf);
            mp_raise_msg_varg(
                &mp_type_MemoryError,
                MP_ERROR_TEXT("Not enough SRAM DMA memory (%d)"),
                size
            );
            return mp_const_none;
        }
        self->panel_io_config.flags.bb_invalidate_cache = true;
        self->panel_io_config.bounce_buffer_size_px = size;
        return MP_OBJ_FROM_PTR(view);
    }
}


mp_lcd_err_t led_init(mp_obj_t obj, uint16_t width, uint16_t height, uint8_t bpp, uint32_t buffer_size, bool rgb565_byte_swap, uint8_t cmd_bits, uint8_t param_bits)
{
    LCD_UNUSED(cmd_bits);
    LCD_UNUSED(param_bits);
    LCD_UNUSED(rgb565_byte_swap);

    mp_lcd_led_bus_obj_t *self = (mp_lcd_led_bus_obj_t *)obj;

    if (bpp != 24) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Must set bpp to 24"));
        return LCD_ERR_INVALID_ARG;
    }

    self->pixel_count = width * height;
    self->buffer_size = self->leds_per_pixel * self->pixel_count;

    if (self->spi_device == NULL) {
        rmt_tx_channel_config_t rmt_chan_config = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .gpio_num = self->data_pin,
            .mem_block_symbols = 64,
            .resolution_hz = self->freq,
            .trans_queue_depth = 4,
            .flags.with_dma = self->buf2 != NULL ? 1:0,
            .flags.invert_out = 0,
        };

        ESP_ERROR_CHECK(rmt_new_tx_channel(&rmt_chan_config, &self->rmt_chan));

        self->strip_encoder = calloc(1, sizeof(mp_lcd_led_strip_encoder_t));
        self->strip_encoder->base.encode = led_encode_strip;
        self->strip_encoder->base.del = led_del_strip;
        self->strip_encoder->base.reset = led_reset_strip;

        uint16_t bit0_level0 = self->bit0.high >= 0 ? 1:0;
        uint16_t bit0_level1 = self->bit0.high >= 0 ? 1:0;

        uint16_t bit0_duration0 = self->bit0.high >= 0 ? (uint16_t)self->bit0.high:(uint16_t)-self->bit0.high;
        uint16_t bit0_duration1 = self->bit0.low >= 0 ? (uint16_t)self->bit0.low:(uint16_t)-self->bit0.low;

        uint16_t bit1_level0 = self->bit1.high >= 0 ? 1:0;
        uint16_t bit1_level1 = self->bit1.high >= 0 ? 1:0;

        uint16_t bit1_duration0 = self->bit1.high >= 0 ? (uint16_t)self->bit1.high:(uint16_t)-self->bit1.high;
        uint16_t bit1_duration1 = self->bit1.low >= 0 ? (uint16_t)self->bit1.low:(uint16_t)-self->bit1.low;

        rmt_bytes_encoder_config_t bytes_encoder_config = (rmt_bytes_encoder_config_t) {
            .bit0 = {
                .level0 = bit0_level0,
                .duration0 = bit0_duration0 * self->freq / 1000000000,
                .level1 = bit0_level1,
                .duration1 = bit0_duration1 * self->freq / 1000000000,
            },
            .bit1 = {
                .level0 = bit1_level0,
                .duration0 = bit1_duration0 * self->freq / 1000000000,
                .level1 = bit1_level1,
                .duration1 = bit1_duration1 * self->freq / 1000000000,
            },
            .flags.msb_first = (uint32_t)self->msb_first;
        };

        ESP_ERROR_CHECK(rmt_new_bytes_encoder(&bytes_encoder_config, $self->strip_encoder->bytes_encoder));
        rmt_copy_encoder_config_t copy_encoder_config = {};
        ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_config, &self->strip_encoder->copy_encoder));

        uint32_t reset_ticks = self->freq / 1000000 * self->res / 2;

        self->strip_encoder->reset_code = (rmt_symbol_word_t) {
            .level0 = 0,
            .duration0 = reset_ticks,
            .level1 = 0,
            .duration1 = reset_ticks,
        };

        rmt_tx_event_callbacks_t callback = {
            .on_trans_done = &led_rmt_bus_trans_done_cb
        }

        rmt_tx_register_event_callbacks(self->rmt_chan, &callback, self);
        ESP_ERROR_CHECK(rmt_enable(self->rmt_chan));

    } else {
        if (self->spi_device->spi_bus->state == MP_SPI_STATE_STOPPED) {
            machine_hw_spi_bus_initilize(self->spi_device->spi_bus);
        }

        mp_lcd_err_t ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)self->spi_device->spi_bus->host, self->panel_io_config, &self->panel_io_handle.panel_io);
        if (ret != ESP_OK) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%d(esp_lcd_new_panel_io_spi)"), ret);
        }

        machine_hw_spi_bus_add_device(*self->spi_device);

        self->buffer_size = self->pixel_count * 4 + 8;
    }

    void *buf1 = self->view1->items;
    self->view1->items = heaps_caps_calloc(1, self->buffer_size, self->buffer_flags);
    self->buf1 = self->view1->items;
    self->view1->len = self->buffer_size;
    heap_caps_free(buf1);

    if (self->buf2 != NULL) {
        void *buf2 = self->view2->items;
        self->view2->items = heaps_caps_calloc(1, self->buffer_size, self->buffer_flags);
        self->buf2 = self->view2->items;
        self->view2->len = self->buffer_size;
        heap_caps_free(buf2);
    }

    return LCD_OK;
}

mp_lcd_err_t led_get_lane_count(mp_obj_t obj, uint8_t *lane_count)
{
    mp_lcd_led_bus_obj_t *self = (mp_lcd_led_bus_obj_t *)obj;
    *lane_count = 1;

    return LCD_OK;
}


mp_lcd_err_t led_tx_color(mp_obj_t obj, int lcd_cmd, void *color, size_t color_size, int x_start, int y_start, int x_end, int y_end)
{
    LCD_UNUSED(lcd_cmd);
    LCD_UNUSED(x_start);
    LCD_UNUSED(y_start);
    LCD_UNUSED(x_end);
    LCD_UNUSED(y_end);
    mp_lcd_led_bus_obj_t *self = (mp_lcd_led_bus_obj_t *)obj;
    mp_lcd_err_t err;
    uint8_t tmp_color[4];

    if (self->spi_device == NULL) {
        if (self->leds_per_pixel == 4) {
            for (uint32_t i = color_size - 3;i >= 0;i -= 3) {
                for (uint8_t j = 0; j < 3; j++) tmp_color[j] = color[i + j];
                rgb2rgbw(&self->color_temp, tmp_color);
                for (uint8_t j = 0; j < 3; j++) color[i + rgb_order[j] + self->pixel_count] = tmp_color[j];
                color[i + 4 + self->pixel_count] = tmp_color[3];
            }
        } else {
            if (pixel_order != RGB) {
                for (uint32_t i = 0;i < color_size;i += 3) {
                    for (uint8_t j = 0; j < 3; j++) tmp_color[rgb_order[j]] = color[i + j];
                    for (uint8_t j = 0; j < 3; j++) color[i + j] = tmp_color[j];
                }
            }
        }
        rmt_transmit_config_t tx_conf = {
            .loop_count = 0,
        };
        err = rmt_transmit(self->rmt_chan, self->strip_encoder, color, color_size, &tx_conf);
    } else {
        for (uint32_t i = color_size - 3;i >= 0;i -= 3) {
            for (uint8_t j = 0; j < 3; j++) tmp_color[j] = color[i + j];
            rgb2rgbw(&self->color_temp, tmp_color);
            for (uint8_t j = 0; j < 3; j++) color[i + rgb_order[j] + self->pixel_count + 5] = tmp_color[j];
            color[i + self->pixel_count + 4] = tmp_color[3] | 0xE0;
        }
        for (uint8_t i = 0;i < 4;i++) {
            color[i] = 0x00;
            color[i + self->pixel_count * 4] = 0xFF
        }

        color_size = (size_t)self->pixel_count * 4 + 8;

        err = esp_lcd_panel_io_tx_color(self->panel_io_handle.panel_io, -1, color, color_size);
    }

    if (err == LCD_OK && self->callback == mp_const_none) {
        while (!self->trans_done) {}
        self->trans_done = false;
    }

    return err;
}

