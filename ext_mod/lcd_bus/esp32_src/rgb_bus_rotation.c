#include "soc/soc_caps.h"

#if SOC_LCD_RGB_SUPPORTED
    // micropython includes
    #include "py/obj.h"
    #include "py/runtime.h"
    #include "py/gc.h"

    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "freertos/semphr.h"
    #include "freertos/event_groups.h"
    #include "freertos/idf_additions.h"
    #include "rom/ets_sys.h"
    #include "esp_system.h"
    #include "esp_cpu.h"

    #include "esp_lcd_panel_ops.h"

    #include "rgb_bus.h"

    #include <string.h>

    #define RGB_BIT_0 (1 << 0)


    void rgb_bus_event_init(rgb_bus_event_t *event)
    {
    #if CONFIG_LCD_ENABLE_DEBUG_LOG
        mp_printf(&mp_plat_print, "rgb_bus_event_init\n");
    #endif
        event->handle = xEventGroupCreateStatic(&event->buffer);
    }

    void rgb_bus_event_delete(rgb_bus_event_t *event)
    {
    #if CONFIG_LCD_ENABLE_DEBUG_LOG
        mp_printf(&mp_plat_print, "rgb_bus_event_delete\n");
    #endif
        xEventGroupSetBits(event->handle, RGB_BIT_0);
        vEventGroupDelete(event->handle);

    }


    bool rgb_bus_event_isset(rgb_bus_event_t *event)
    {
    #if CONFIG_LCD_ENABLE_DEBUG_LOG
        mp_printf(&mp_plat_print, "rgb_bus_event_isset\n");
    #endif
        return (bool)(xEventGroupGetBits(event->handle) & RGB_BIT_0);
    }


    bool rgb_bus_event_isset_from_isr(rgb_bus_event_t *event)
    {
        return (bool)(xEventGroupGetBitsFromISR(event->handle) & RGB_BIT_0);
    }


    void rgb_bus_event_set(rgb_bus_event_t *event)
    {
    #if CONFIG_LCD_ENABLE_DEBUG_LOG
        mp_printf(&mp_plat_print, "rgb_bus_event_set\n");
    #endif
        xEventGroupSetBits(event->handle, RGB_BIT_0);
    }


    void rgb_bus_event_clear(rgb_bus_event_t *event)
    {
    #if CONFIG_LCD_ENABLE_DEBUG_LOG
        mp_printf(&mp_plat_print, "rgb_bus_event_clear\n");
    #endif
        xEventGroupClearBits(event->handle, RGB_BIT_0);
    }

    void rgb_bus_event_clear_from_isr(rgb_bus_event_t *event)
    {
        xEventGroupClearBitsFromISR(event->handle, RGB_BIT_0);
    }

    void rgb_bus_event_set_from_isr(rgb_bus_event_t *event)
    {
        xEventGroupSetBitsFromISR(event->handle, RGB_BIT_0, pdFALSE);
    }


    int rgb_bus_lock_acquire(rgb_bus_lock_t *lock, int32_t wait_ms)
    {
    #if CONFIG_LCD_ENABLE_DEBUG_LOG
        mp_printf(&mp_plat_print, "rgb_bus_lock_acquire\n");
    #endif
        return pdTRUE == xSemaphoreTake(lock->handle, wait_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS((uint16_t)wait_ms));
    }


    void rgb_bus_lock_release(rgb_bus_lock_t *lock)
    {
    #if CONFIG_LCD_ENABLE_DEBUG_LOG
        mp_printf(&mp_plat_print, "rgb_bus_lock_release\n");
    #endif
        xSemaphoreGive(lock->handle);
    }


    void rgb_bus_lock_release_from_isr(rgb_bus_lock_t *lock)
    {
        xSemaphoreGiveFromISR(lock->handle, pdFALSE);
    }


    void rgb_bus_lock_init(rgb_bus_lock_t *lock)
    {
    #if CONFIG_LCD_ENABLE_DEBUG_LOG
        mp_printf(&mp_plat_print, "rgb_bus_lock_init\n");
    #endif
        lock->handle = xSemaphoreCreateBinaryStatic(&lock->buffer);
        xSemaphoreGive(lock->handle);
    }


    void rgb_bus_lock_delete(rgb_bus_lock_t *lock)
    {
    #if CONFIG_LCD_ENABLE_DEBUG_LOG
        mp_printf(&mp_plat_print, "rgb_bus_lock_delete\n");
    #endif
        vSemaphoreDelete(lock->handle);
    }

    #define RGB_BUS_ROTATION_0    (0)
    #define RGB_BUS_ROTATION_90   (1)
    #define RGB_BUS_ROTATION_180  (2)
    #define RGB_BUS_ROTATION_270  (3)

    typedef void (* copy_func_cb_t)(uint8_t *to, const uint8_t *from);

    static void copy_pixels(
                uint8_t *to, uint8_t *from, uint32_t x_start, uint32_t y_start,
                uint32_t x_end, uint32_t y_end, uint32_t h_res, uint32_t v_res,
                uint32_t bytes_per_pixel, copy_func_cb_t func, uint8_t rotate);


    __attribute__((always_inline))
    static inline void copy_8bpp(uint8_t *to, const uint8_t *from)
    {
        *to++ = *from++;
    }

    __attribute__((always_inline))
    static inline void copy_16bpp(uint8_t *to, const uint8_t *from)
    {
        *to++ = *from++;
        *to++ = *from++;
    }

    __attribute__((always_inline))
    static inline void copy_24bpp(uint8_t *to, const uint8_t *from)
    {
        *to++ = *from++;
        *to++ = *from++;
        *to++ = *from++;
    }

    void rgb_bus_copy_task(void *self_in) {
    #if CONFIG_LCD_ENABLE_DEBUG_LOG
        mp_printf(&mp_plat_print, "rgb_bus_copy_task - STARTED\n");
    #endif

        mp_lcd_rgb_bus_obj_t *self = (mp_lcd_rgb_bus_obj_t *)self_in;

        copy_func_cb_t func;
        uint8_t bytes_per_pixel = self->bytes_per_pixel;

        switch (bytes_per_pixel) {
            case 1:
                func = copy_8bpp;
                break;
            case 2:
                func = copy_16bpp;
                break;
            case 3:
                func = copy_24bpp;
                break;
            default:
                // raise error
                return;
        }

        // we acquire both of these locks once so the next time they are acquired
        // it will stall. the areas that release them do so only to allow the code
        // to run a single time. when the lock is acquired the next loop around
        // it will stall the task amnd yield so other work is able to be done.
        rgb_bus_lock_acquire(&self->copy_lock, -1);
        rgb_bus_lock_acquire(&self->swap_lock, -1);

        bool exit = rgb_bus_event_isset(&self->copy_task_exit);

        while (!exit) {
            rgb_bus_lock_acquire(&self->copy_lock, -1);
            #if CONFIG_LCD_ENABLE_DEBUG_LOG
                mp_printf(&mp_plat_print, "!rgb_bus_event_isset(&self->copy_task_exit)\n");
            #endif

            if (self->partial_buf == NULL) break;

            copy_pixels(
                self->idle_fb, self->partial_buf,
                self->x_start, self->y_start,
                self->x_end, self->y_end,
                self->width, self->height,
                bytes_per_pixel, func, self->rotation);

            if (self->callback != mp_const_none) {
                volatile uint32_t sp = (uint32_t)esp_cpu_get_sp();

                void *old_state = mp_thread_get_state();

                mp_state_thread_t ts;
                mp_thread_set_state(&ts);
                mp_stack_set_top((void*)sp);
                mp_stack_set_limit(CONFIG_FREERTOS_IDLE_TASK_STACKSIZE - 1024);
                mp_locals_set(mp_state_ctx.thread.dict_locals);
                mp_globals_set(mp_state_ctx.thread.dict_globals);

                mp_sched_lock();
                gc_lock();

                nlr_buf_t nlr;
                if (nlr_push(&nlr) == 0) {
                    mp_call_function_n_kw(self->callback, 0, 0, NULL);
                    nlr_pop();
                } else {
                    ets_printf("Uncaught exception in IRQ callback handler!\n");
                    mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
                }

                gc_unlock();
                mp_sched_unlock();

                mp_thread_set_state(old_state);
            }

            if (rgb_bus_event_isset(&self->last_update)) {
                rgb_bus_event_clear(&self->last_update);
                // the reason why this locked is released this way is to ensure that the partial
                // buffer has been copied correctly before another one gets into the queue
                // it is places here after the partial check to ensure the setting of the partial doesn't overlap
                // with the wrong partial buffer
                rgb_bus_lock_release(&self->tx_color_lock);
            #if CONFIG_LCD_ENABLE_DEBUG_LOG
                mp_printf(&mp_plat_print, "rgb_bus_event_isset(&self->last_update)\n");
            #endif

                uint8_t *idle_fb = self->idle_fb;
                rgb_bus_event_set(&self->swap_bufs);

                mp_lcd_err_t ret = esp_lcd_panel_draw_bitmap(
                    self->panel_handle,
                    0,
                    0,
                    self->width - 1,
                    self->height - 1,
                    idle_fb
                );

                if (ret != 0) {
                    mp_printf(&mp_plat_print, "esp_lcd_panel_draw_bitmap error (%d)\n", ret);
                } else {
                    rgb_bus_lock_acquire(&self->swap_lock, -1);
                    memcpy(self->idle_fb, self->active_fb, self->width * self->height * bytes_per_pixel);
                }
            } else {
                rgb_bus_lock_release(&self->tx_color_lock);
            }

            exit = rgb_bus_event_isset(&self->copy_task_exit);
        }

    #if CONFIG_LCD_ENABLE_DEBUG_LOG
        mp_printf(&mp_plat_print, "rgb_bus_copy_task - STOPPED\n");
    #endif
    }


    void copy_pixels(
        uint8_t *to,
        uint8_t *from,
        uint32_t x_start,
        uint32_t y_start,
        uint32_t x_end,
        uint32_t y_end,
        uint32_t h_res,
        uint32_t v_res,
        uint32_t bytes_per_pixel,
        copy_func_cb_t func,
        uint8_t rotate
    ) {
    #if CONFIG_LCD_ENABLE_DEBUG_LOG
        mp_printf(&mp_plat_print, "copy_pixels(to, from, x_start=%lu, y_start=%lu, x_end=%lu, y_end=%lu, h_res=%lu, v_res=%lu, bytes_per_pixel=%lu, func, rotate=%u)\n",
                x_start, y_start, x_end, y_end, h_res, v_res, bytes_per_pixel, rotate);
    #endif
        if (rotate == RGB_BUS_ROTATION_90 || rotate == RGB_BUS_ROTATION_270) {
            x_start = MIN(x_start, v_res);
            x_end = MIN(x_end, v_res);
            y_start = MIN(y_start, h_res);
            y_end = MIN(y_end, h_res);
        } else {
            x_start = MIN(x_start, h_res);
            x_end = MIN(x_end, h_res);
            y_start = MIN(y_start, v_res);
            y_end = MIN(y_end, v_res);
        }

        uint16_t copy_bytes_per_line = (x_end - x_start + 1) * (uint16_t)bytes_per_pixel;
        int pixels_per_line = h_res;
        uint32_t bytes_per_line = bytes_per_pixel * pixels_per_line;
        size_t offset = y_start * copy_bytes_per_line + x_start * bytes_per_pixel;

    #if CONFIG_LCD_ENABLE_DEBUG_LOG
        mp_printf(&mp_plat_print, "x_start=%lu, y_start=%lu, x_end=%lu, y_end=%lu, copy_bytes_per_line=%hu, bytes_per_line=%lu, offset=%d\n",
                x_start, y_start, x_end, y_end, copy_bytes_per_line, bytes_per_line, offset);
    #endif

        switch (rotate) {
            case RGB_BUS_ROTATION_0:
            #if CONFIG_LCD_ENABLE_DEBUG_LOG
                mp_printf(&mp_plat_print, "RGB_BUS_ROTATION_0\n");
            #endif
                uint8_t *fb = to + (y_start * h_res + x_start) * bytes_per_pixel;

                if (x_start == 0 && x_end == (h_res - 1)) {
                    memcpy(fb, from, bytes_per_line * (y_end - y_start + 1));
                } else {
                    for (int y = y_start; y < y_end; y++) {
                        memcpy(fb, from, bytes_per_line);
                        fb += bytes_per_line;
                        from += copy_bytes_per_line;
                    }
                }

                break;
            case RGB_BUS_ROTATION_180:
            #if CONFIG_LCD_ENABLE_DEBUG_LOG
                mp_printf(&mp_plat_print, "RGB_BUS_ROTATION_180\n");
            #endif
                uint32_t index;
                for (int y = y_start; y < y_end; y++) {
                    index = ((v_res - 1 - y) * h_res + (h_res - 1 - x_start)) * bytes_per_pixel;
                    for (size_t x = x_start; x < x_end; x++) {
                        func(to + index, from);
                        index -= bytes_per_pixel;
                        from += bytes_per_pixel;
                    }
                }
                break;

            case RGB_BUS_ROTATION_90:
            #if CONFIG_LCD_ENABLE_DEBUG_LOG
                mp_printf(&mp_plat_print, "RGB_BUS_ROTATION_90\n");
            #endif
                uint32_t j;
                uint32_t i;

                for (int y = y_start; y < y_end; y++) {
                    for (int x = x_start; x < x_end; x++) {
                        j = y * copy_bytes_per_line + x * bytes_per_pixel - offset;
                        i = (x * h_res + y) * bytes_per_pixel;
                        func(to + i, from + j);
                    }
                }
                break;



            case RGB_BUS_ROTATION_270:
            #if CONFIG_LCD_ENABLE_DEBUG_LOG
                mp_printf(&mp_plat_print, "RGB_BUS_ROTATION_270\n");
            #endif
                uint32_t jj;
                uint32_t ii;

                for (int y = y_start; y < y_end; y++) {
                    for (int x = x_start; x < x_end; x++) {
                        jj = y * copy_bytes_per_line + x * bytes_per_pixel - offset;
                        ii = ((v_res - 1 - x) * h_res + y) * bytes_per_pixel;
                        func(to + ii, from + jj);
                    }
                }
                break;

            default:
                break;
        }
    }

#endif