import time
from micropython import const  # NOQA

import lvgl as lv  # NOQA
import lcd_bus  # NOQA
import display_driver_framework


_SWRESET = const(0x01)
_SLPOUT = const(0x11)
_MADCTL = const(0x36)
_COLMOD = const(0x3A)
_IFMODE = const(0xB0)
_PORCTRL = const(0xB2)
_GCTRL = const(0xB7)
_VCOMS = const(0xBB)
_LCMCTRL = const(0xC0)
_VDVVRHEN = const(0xC2)
_VRHS = const(0xC3)
_VDVSET = const(0xC4)
_FRCTR2 = const(0xC6)
_PWCTRL1 = const(0xD0)
_INVON = const(0x21)
_CASET = const(0x2A)
_RASET = const(0x2B)
_PGC = const(0xE0)
_NGC = const(0xE1)
_DISPON = const(0x29)
_NORON = const(0x13)

STATE_HIGH = display_driver_framework.STATE_HIGH
STATE_LOW = display_driver_framework.STATE_LOW
STATE_PWM = display_driver_framework.STATE_PWM

BYTE_ORDER_RGB = display_driver_framework.BYTE_ORDER_RGB
BYTE_ORDER_BGR = display_driver_framework.BYTE_ORDER_BGR

_MADCTL_MH = const(0x04)  # Refresh 0=Left to Right, 1=Right to Left
_MADCTL_BGR = const(0x08)  # BGR color order
_MADCTL_ML = const(0x10)  # Refresh 0=Top to Bottom, 1=Bottom to Top
_MADCTL_MV = const(0x20)  # 0=Normal, 1=Row/column exchange
_MADCTL_MX = const(0x40)  # 0=Left to Right, 1=Right to Left
_MADCTL_MY = const(0x80)  # 0=Top to Bottom, 1=Bottom to Top






class ST7789(display_driver_framework.DisplayDriver):
    _ORIENTATION_TABLE = (
        0x0,
        _MADCTL_MV | _MADCTL_MY,
        _MADCTL_MY | _MADCTL_MX,
        _MADCTL_MV | _MADCTL_MX
    )

    def init(self):
        param_buf = bytearray(14)
        param_mv = memoryview(param_buf)

        self.set_params(_SWRESET)

        time.sleep_ms(120)  # NOQA

        self.set_params(_SLPOUT)

        time.sleep_ms(120)  # NOQA

        self.set_params(_NORON)

        param_buf[0] = (
            self._madctl(
                self._color_byte_order,
                self._ORIENTATION_TABLE  # NOQA
            )
        )
        self.set_params(_MADCTL, param_mv[:1])

        param_buf[0] = 0x0A
        param_buf[1] = 0x82
        self.set_params(0xB6, param_mv[:2])

        # param_buf[0] = 0x00
        # param_buf[1] = 0xE0
        # self.set_params(_IFMODE, param_mv[:2])

        color_size = lv.color_format_get_size(self._color_space)
        if color_size == 2:  # NOQA
            pixel_format = 0x55
        elif color_size == 3:
            pixel_format = 0x77
        else:
            raise RuntimeError(
                f'{self.__class__.__name__} IC only supports '
                'lv.COLOR_FORMAT.RGB565 or lv.COLOR_FORMAT.RGB888'
            )

        param_buf[0] = pixel_format
        self.set_params(_COLMOD, param_mv[:1])

        time.sleep_ms(10)  # NOQA

        param_buf[0] = 0x0C
        param_buf[1] = 0x0C
        param_buf[2] = 0x00
        param_buf[3] = 0x33
        param_buf[4] = 0x33
        self.set_params(_PORCTRL, param_mv[:5])

        param_buf[0] = 0x35
        self.set_params(_GCTRL, param_mv[:1])

        param_buf[0] = 0x28
        self.set_params(_VCOMS, param_mv[:1])

        param_buf[0] = 0x0C
        self.set_params(_LCMCTRL, param_mv[:1])

        param_buf[0] = 0x01
        self.set_params(_VDVVRHEN, param_mv[:1])

        param_buf[0] = 0x13
        self.set_params(_VRHS, param_mv[:1])

        param_buf[0] = 0x20
        self.set_params(_VDVSET, param_mv[:1])

        param_buf[0] = 0x0F
        self.set_params(_FRCTR2, param_mv[:1])

        param_buf[0] = 0xA4
        param_buf[1] = 0xA1
        self.set_params(_PWCTRL1, param_mv[:2])

        param_buf[0] = 0xD0
        param_buf[1] = 0x00
        param_buf[2] = 0x02
        param_buf[3] = 0x07
        param_buf[4] = 0x0A
        param_buf[5] = 0x28
        param_buf[6] = 0x32
        param_buf[7] = 0x44
        param_buf[8] = 0x42
        param_buf[9] = 0x06
        param_buf[10] = 0x0E
        param_buf[11] = 0x12
        param_buf[12] = 0x14
        param_buf[13] = 0x17
        self.set_params(_PGC, param_mv[:14])

        param_buf[0] = 0xD0
        param_buf[1] = 0x00
        param_buf[2] = 0x02
        param_buf[3] = 0x07
        param_buf[4] = 0x0A
        param_buf[5] = 0x28
        param_buf[6] = 0x31
        param_buf[7] = 0x54
        param_buf[8] = 0x47
        param_buf[9] = 0x0E
        param_buf[10] = 0x1C
        param_buf[11] = 0x17
        param_buf[12] = 0x1B
        param_buf[13] = 0x1E
        self.set_params(_NGC, param_mv[:14])

        self.set_params(_INVON)

        param_buf[0] = 0x00
        param_buf[1] = 0x00
        param_buf[2] = (self.display_width >> 8) & 0xFF
        param_buf[3] = self.display_width & 0xFF

        self.set_params(_CASET, param_mv[:4])

        # Page addresses
        param_buf[0] = 0x00
        param_buf[1] = 0x00
        param_buf[2] = (self.display_height >> 8) & 0xFF
        param_buf[3] = self.display_height & 0xFF

        self.set_params(_RASET, param_mv[:4])

        self.set_params(_DISPON)
        time.sleep_ms(120)  # NOQA

        self.set_params(_SLPOUT)
        time.sleep_ms(120)  # NOQA

        display_driver_framework.DisplayDriver.init(self)
