#ifndef _ESP32_I80_BUS_H_
    #define _ESP32_I80_BUS_H_

    //local_includes
    #include "lcd_types.h"

    // micropython includes
    #include "mphalport.h"
    #include "py/obj.h"
    #include "py/objarray.h"

    // esp-idf includes
    #include "soc/soc_caps.h"

    #if SOC_LCD_I80_SUPPORTED
        // esp-idf includes
        #include "esp_lcd_panel_io.h"
        #include "spi_3wire.h"


        typedef struct _mp_lcd_i80_bus_obj_t {
            mp_obj_base_t base;

            mp_obj_t callback;

            void *buf1;
            void *buf2;
            uint32_t buffer_flags;

            bool trans_done;
            bool rgb565_byte_swap;

            lcd_panel_io_t panel_io_handle;

            esp_lcd_panel_io_i80_config_t panel_io_config;
            esp_lcd_i80_bus_config_t bus_config;
            esp_lcd_i80_bus_handle_t bus_handle;

            mp_spi_3wire_obj_t *spi_3wire;

        } mp_lcd_i80_bus_obj_t;

        extern const mp_obj_type_t mp_lcd_i80_bus_type;

    #else
        #include "../common_include/i80_bus.h"

    #endif /* SOC_LCD_I80_SUPPORTED */
#endif /* _ESP32_I80_BUS_H_ */
