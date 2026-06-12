import sys
import os
import struct
import time
import threading
import serial
import serial.tools.list_ports

from PyQt6.QtWidgets import (
    QApplication,
    QWidget,
    QPushButton,
    QLabel,
    QFileDialog,
    QVBoxLayout,
    QHBoxLayout,
    QTextEdit,
    QProgressBar,
    QComboBox,
    QMessageBox,
    QLineEdit,
    QFormLayout
)

from PyQt6.QtCore import pyqtSignal, QObject


PORT_BAUD = 115200
MAX_RETRIES = 5

# APP_ADDR = 0x08040000
CHUNK_SIZE = 4096


# ==========================================================
# CRC
# ==========================================================

def stm32_crc32(data_words):
    crc = 0xFFFFFFFF

    for word in data_words:
        crc ^= word

        for _ in range(32):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF

    return crc


def calculate_image_crc_size(firmware):

    padded = firmware

    if len(padded) % 4:
        padded += bytes(4 - (len(padded) % 4))

    data_words = []

    for i in range(0, len(padded), 4):
        data_words.append(
            struct.unpack("<I", padded[i:i + 4])[0]
        )

    return stm32_crc32(data_words)


# ==========================================================
# PACKETS
# ==========================================================

def build_packet(address, payload, pkt_type):

    length = len(payload)

    if length % 4:
        payload += bytes(4 - (length % 4))
        length = len(payload)

    data_words = []

    for i in range(0, length, 4):
        data_words.append(
            struct.unpack("<I", payload[i:i + 4])[0]
        )

    crc = stm32_crc32(data_words)

    packet = bytearray()

    packet += b"\xAA\x55"
    packet += struct.pack("<B", pkt_type)
    packet += struct.pack("<I", address)
    packet += struct.pack("<H", length)
    packet += payload
    packet += struct.pack("<I", crc)

    return packet


def build_eoc_packet(image_crc, firmware_size):

    packet_crc = stm32_crc32([
        firmware_size,
        image_crc
    ])

    packet = bytearray()

    packet += b"\xAA\x55"
    packet += b"\x11"

    packet += struct.pack("<I", firmware_size)
    packet += struct.pack("<I", image_crc)
    packet += struct.pack("<I", packet_crc)

    return packet


def get_packet_type(offset):

    if offset == 0:
        return 0x01

    return 0x10


# ==========================================================
# SIGNALS
# ==========================================================

class WorkerSignals(QObject):

    log = pyqtSignal(str)
    progress = pyqtSignal(int)
    finished = pyqtSignal(bool, str)


# ==========================================================
# PROGRAMMER THREAD
# ==========================================================

class ProgrammerThread(threading.Thread):

    def __init__(
        self,
        file_path,
        port,
        app_addr,
        signals
    ):

        super().__init__()

        self.file_path = file_path
        self.port = port
        self.app_addr = app_addr
        self.signals = signals

    def log(self, msg):
        self.signals.log.emit(msg)

    def send_packet_with_retry(
        self,
        ser,
        packet,
        description
    ):

        for attempt in range(MAX_RETRIES):

            self.log(
                f"{description} "
                f"(Attempt {attempt+1})"
            )

            ser.reset_input_buffer()

            ser.write(packet)
            ser.flush()

            response = ser.read(128).decode(
                errors="ignore"
            )

            self.log(
                f"Response: {response.strip()}"
            )

            if "PACKET RECEIVED" in response:
                return True

            time.sleep(0.1)

        return False

    def run(self):

        try:

            with open(self.file_path, "rb") as f:
                firmware = f.read()

            firmware_size = len(firmware)

            image_crc = calculate_image_crc_size(
                firmware
            )

            self.log(
                f"Firmware Size : {firmware_size}"
            )

            self.log(
                f"Image CRC     : 0x{image_crc:08X}"
            )

            ser = serial.Serial(
                self.port,
                PORT_BAUD,
                timeout=2
            )

            offset = 0

            total_packets = (
                firmware_size +
                CHUNK_SIZE - 1
            ) // CHUNK_SIZE

            packet_num = 0

            start_time = time.time()

            while offset < firmware_size:

                packet_num += 1

                chunk = firmware[
                    offset:
                    offset + CHUNK_SIZE
                ]

                packet = build_packet(
                    self.app_addr + offset,
                    chunk,
                    get_packet_type(offset)
                )

                success = self.send_packet_with_retry(
                    ser,
                    packet,
                    f"Packet {packet_num}/{total_packets}"
                )

                if not success:

                    ser.close()

                    self.signals.finished.emit(
                        False,
                        f"Failed at offset 0x{offset:08X}"
                    )

                    return

                offset += len(chunk)

                percent = int(
                    offset * 100 /
                    firmware_size
                )

                self.signals.progress.emit(
                    percent
                )

            self.log("Sending EOC...")

            eoc = build_eoc_packet(
                image_crc,
                firmware_size
            )

            success = self.send_packet_with_retry(
                ser,
                eoc,
                "EOC"
            )

            ser.close()

            elapsed = (
                time.time() -
                start_time
            )

            if success:

                self.log(
                    f"Transfer Time: "
                    f"{elapsed:.2f}s"
                )

                self.signals.finished.emit(
                    True,
                    "Programming Successful"
                )

            else:

                self.signals.finished.emit(
                    False,
                    "EOC Failed"
                )

        except Exception as e:

            self.signals.finished.emit(
                False,
                str(e)
            )

# ==========================================================
# GUI
# ==========================================================

class MainWindow(QWidget):

    def __init__(self):

        super().__init__()

        self.setWindowTitle(
            "STM32 Firmware Updater"
        )

        self.resize(700, 500)

        self.file_label = QLabel(
            "No file selected"
        )

        self.port_combo = QComboBox()
        
        self.addr_input = QLineEdit()
        self.addr_input.setText("0x08040000")

        self.refresh_ports()

        browse_btn = QPushButton(
            "Select Firmware"
        )

        browse_btn.clicked.connect(
            self.select_file
        )

        refresh_btn = QPushButton(
            "Refresh Ports"
        )

        refresh_btn.clicked.connect(
            self.refresh_ports
        )

        self.start_btn = QPushButton(
            "Start Programming"
        )

        self.start_btn.clicked.connect(
            self.start_programming
        )

        self.progress = QProgressBar()

        self.log_box = QTextEdit()
        self.log_box.setReadOnly(True)

        layout = QVBoxLayout()

        form = QFormLayout()

        form.addRow(
            "Application Address:",
            self.addr_input
        )

        layout.addLayout(form)

        layout.addWidget(self.file_label)

        file_row = QHBoxLayout()
        file_row.addWidget(browse_btn)

        layout.addLayout(file_row)

        port_row = QHBoxLayout()
        port_row.addWidget(self.port_combo)
        port_row.addWidget(refresh_btn)

        layout.addLayout(port_row)

        layout.addWidget(self.start_btn)
        layout.addWidget(self.progress)
        layout.addWidget(self.log_box)

        self.setLayout(layout)

        self.file_path = None

    def refresh_ports(self):

        self.port_combo.clear()

        for port in serial.tools.list_ports.comports():
            self.port_combo.addItem(
                port.device
            )

    def select_file(self):

        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "Select Firmware",
            "",
            "Binary Files (*.bin)"
        )

        if file_path:

            self.file_path = file_path

            self.file_label.setText(
                os.path.basename(file_path)
            )

    def append_log(self, msg):

        self.log_box.append(msg)

    def start_programming(self):

        if not self.file_path:

            QMessageBox.warning(
                self,
                "Error",
                "Select firmware first"
            )

            return

        port = self.port_combo.currentText()

        try:
            app_addr = int(
                self.addr_input.text().strip(),
                16
            )
        except ValueError:

            QMessageBox.warning(
                self,
                "Error",
                "Invalid address.\nExample: 0x08040000"
            )

            return

        self.start_btn.setEnabled(False)

        self.signals = WorkerSignals()

        self.signals.log.connect(
            self.append_log
        )

        self.signals.progress.connect(
            self.progress.setValue
        )

        self.signals.finished.connect(
            self.finished
        )

        self.worker = ProgrammerThread(
            self.file_path,
            port,
            app_addr,
            self.signals
        )

        self.worker.start()

    def finished(
        self,
        success,
        message
    ):

        self.start_btn.setEnabled(True)

        if success:

            QMessageBox.information(
                self,
                "Success",
                message
            )

        else:

            QMessageBox.critical(
                self,
                "Failed",
                message
            )


if __name__ == "__main__":

    app = QApplication(sys.argv)

    window = MainWindow()
    window.show()

    sys.exit(app.exec())