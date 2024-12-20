#ifndef _ESP32_DSI_BUS_H_
    #define _ESP32_DSI_BUS_H_

    //local_includes
    #include "lcd_types.h"

    // micropython includes
    #include "mphalport.h"
    #include "py/obj.h"
    #include "py/objarray.h"

    // esp-idf includes
    #include "soc/soc_caps.h"


    #if SOC_MIPI_DSI_SUPPORTED
        // esp-idf includes
        #include "esp_lcd_panel_io.h"
        #include "esp_lcd_panel_interface.h"
        #include "esp_lcd_panel_io.h"
        #include "esp_lcd_mipi_dsi.h"


        typedef struct _mp_lcd_dsi_bus_obj_t {
            mp_obj_base_t base;

            rotation_t *rotation;

            mp_obj_t callback;

            mp_obj_array_t *view1;
            mp_obj_array_t *view2;

            uint32_t buffer_flags;

            uint8_t trans_done : 1;
            uint8_t rgb565_byte_swap : 1;

            lcd_panel_io_t panel_io_handle;

            esp_lcd_dbi_io_config_t panel_io_config;
            esp_lcd_dsi_bus_config_t bus_config;
            esp_lcd_dsi_bus_handle_t bus_handle;
            esp_lcd_panel_handle_t panel_handle;
            esp_lcd_dpi_panel_config_t panel_config;

            uint32_t buffer_size;
            mp_obj_array_t *view1;
            mp_obj_array_t *view2;

            void *transmitting_buf;

        } mp_lcd_dsi_bus_obj_t;

        extern const mp_obj_type_t mp_lcd_dsi_bus_type;

    #endif /* SOC_MIPI_DSI_SUPPORTED */
#endif /* _ESP32_DSI_BUS_H_ */
