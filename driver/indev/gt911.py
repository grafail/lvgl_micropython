# this driver uses a special i2c bus implimentation I have written.
# This implimentation takes into consideration the ESP32 and it having
# threading available. It also has some convience methods built into it
# that figure out what is wanting to be done automatically.
# read more about it's use in the stub files.

from micropython import const
import pointer_framework
import machine
import time


_CMD_REG = const(0x8040)
_CMD_CHECK_REG = const(0x8046)
_CMD_RECEIVE = const(0x22)

_ESD_CHECK_REG = const(0x8041)

_CONFIG_START_REG = const(0x8047)

_X_OUTPUT_MAX_LOW_POS = const(0x01)
_X_OUTPUT_MAX_LOW_REG = const(0x8048)

_X_OUTPUT_MAX_HIGH_POS = const(0x02)
_X_OUTPUT_MAX_HIGH_REG = const(0x8049)

_Y_OUTPUT_MAX_LOW_POS = const(0x03)
_Y_OUTPUT_MAX_LOW_REG = const(0x804A)

_Y_OUTPUT_MAX_HIGH_POS = const(0x04)
_Y_OUTPUT_MAX_HIGH_REG = const(0x804B)

_CONFIG_CHKSUM_POS = const(0x88)
_CONFIG_CHKSUM_REG = const(0x80FF)

_CONFIG_FRESH_REG = const(0x8100)

_STATUS_REG = const(0x814E)

_POINT_1_REG = const(0x8150)

_PRODUCT_ID_REG = const(0x8140)
_FIRMWARE_VERSION_REG = const(0x8144)
_VENDOR_ID_REG = const(0x814A)

_X_CORD_RES_REG = const(0x8146)
_Y_CORD_RES_REG = const(0x8148)


_ADDR1 = const(0x5D)
_ADDR2 = const(0x14)


# 0-15 * 32
_VER_SPACE_REG = const(0x805B)  # low 4 bits are bottom and hight is top
_HOR_SPACE_REG = const(0x805C)  # low 4 biits is right and high is left


class GT911(pointer_framework.PointerDriver):

    def __init__(self, i2c_bus, reset_pin=None, interrupt_pin=None, touch_cal=None):
        self._buf = bytearray(6)
        self._mv = memoryview(self._buf)
        self._i2c_bus = i2c_bus
        self._i2c = i2c_bus.add_device(_ADDR1, 16)

        if isinstance(reset_pin, int):
            reset_pin = machine.Pin(reset_pin, machine.Pin.OUT)

        if isinstance(interrupt_pin, int):
            interrupt_pin = machine.Pin(interrupt_pin, machine.Pin.OUT)

        self._reset_pin = reset_pin
        self._interrupt_pin = interrupt_pin

        # self.touch_reset()

        self._i2c.read_mem(_PRODUCT_ID_REG, buf=self._mv[:4])
        print('Product id:', self._buf[:4])

        self._i2c.read_mem(_FIRMWARE_VERSION_REG, buf=self._mv[:2])
        print('Firmware version:', hex(self._buf[0] + (self._buf[1] << 8)))

        self._i2c.read_mem(_VENDOR_ID_REG, buf=self._mv[:1])
        print('Vendor id:', hex(self._buf[0]))

        self._i2c.read_mem(_X_CORD_RES_REG, buf=self._mv[:2])
        print('Configured width:', self._buf[0] + (self._buf[1] << 8))

        self._i2c.read_mem(_Y_CORD_RES_REG, buf=self._mv[:2])
        print('Configured height:', self._buf[0] + (self._buf[1] << 8))

        self._buf[0] = 0x00
        self._i2c.write_mem(_ESD_CHECK_REG, buf=self._mv[:1])

        self._buf[0] = _CMD_RECEIVE
        self._i2c.write_mem(_CMD_CHECK_REG, buf=self._mv[:1])
        self._i2c.write_mem(_CMD_REG, buf=self._mv[:1])

        super().__init__(touch_cal=touch_cal)

    def touch_reset(self):
        if self._interrupt_pin and self._reset_pin:
            self._interrupt_pin.init(self._interrupt_pin.OUT)
            self._interrupt_pin(0)
            self._reset_pin(0)
            time.sleep_ms(10)
            self._interrupt_pin(0)
            time.sleep_ms(1)
            self._reset_pin(1)
            time.sleep_ms(5)
            self._interrupt_pin(0)
            time.sleep_ms(50)
            self._interrupt_pin.init(self._interrupt_pin.IN)
            time.sleep_ms(50)

    def _reflash_config(self, width, height):
        buf = bytearray(185)
        mv = memoryview(buf)

        self._i2c.read_mem(_CONFIG_START_REG, buf=mv)

        buf[_X_OUTPUT_MAX_LOW_POS] = width & 0xFF
        buf[_X_OUTPUT_MAX_HIGH_POS] = (width >> 8) & 0xFF
        buf[_Y_OUTPUT_MAX_LOW_POS] = height & 0xFF
        buf[_Y_OUTPUT_MAX_HIGH_POS] = (height >> 8) & 0xFF

        checksum = ((~sum(buf)) + 1) & 0xFF

        buf[0] = width & 0xFF
        self._i2c.write_mem(_X_OUTPUT_MAX_LOW_REG, buf=mv[:1])

        buf[0] = (width >> 8) & 0xFF
        self._i2c.write_mem(_X_OUTPUT_MAX_HIGH_REG, buf=mv[:1])

        buf[0] = height & 0xFF
        self._i2c.write_mem(_Y_OUTPUT_MAX_LOW_REG, buf=mv[:1])

        buf[0] = (height >> 8) & 0xFF
        self._i2c.write_mem(_Y_OUTPUT_MAX_HIGH_REG, buf=mv[:1])

        buf[0] = checksum
        self._i2c.write_mem(_CONFIG_CHKSUM_REG, buf=mv[:1])

        buf[0] = 0x01
        self._i2c.write_mem(_CONFIG_FRESH_REG, buf=mv[:1])

    def _get_coords(self):
        self._i2c.read_mem(_STATUS_REG, buf=self._mv[:1])
        touch_cnt = self._buf[0] & 0x0F

        if self._buf[0] & 0x80 or touch_cnt < 6:
            self._buf[0] = 0x00
            self._i2c.write_mem(_STATUS_REG, buf=self._mv[:1])

        if touch_cnt == 1:
            self._i2c.read_mem(_POINT_1_REG, buf=self._mv)

            x = self._buf[0] + (self._buf[1] << 8)
            y = self._buf[2] + (self._buf[3] << 8)

            print(x, y)

            self._buf[0] = 0x00
            self._i2c.write_mem(_STATUS_REG, buf=self._mv[:1])

            return self.PRESSED, x, y
