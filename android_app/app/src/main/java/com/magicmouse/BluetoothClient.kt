package com.magicmouse

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.InputStream
import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID

object BluetoothClient {
    private const val TAG = "BluetoothClient"
    val SERVICE_UUID: UUID = UUID.fromString("94f39d29-7d6d-437d-973b-fba39e49d4ee")

    private var socket: BluetoothSocket? = null
    private var outputStream: OutputStream? = null
    private var inputStream: InputStream? = null
    private val scope = CoroutineScope(Dispatchers.IO)

    private val sendChannel = Channel<ByteArray>(Channel.UNLIMITED)
    private val quatChannel = Channel<ByteArray>(Channel.CONFLATED)

    @Volatile
    var isConnected = false
        private set

    @SuppressLint("MissingPermission")
    suspend fun connect(device: BluetoothDevice): Boolean = withContext(Dispatchers.IO) {
        try {
            close()
            Log.d(TAG, "Connecting to Bluetooth device: ${device.name} (${device.address})")
            
            // Create RFCOMM socket to UUID
            val tmpSocket = device.createRfcommSocketToServiceRecord(SERVICE_UUID)
            // Cancel discovery before connecting as it slows down connection
            BluetoothAdapter.getDefaultAdapter()?.cancelDiscovery()
            
            tmpSocket.connect()
            socket = tmpSocket
            outputStream = tmpSocket.outputStream
            inputStream = tmpSocket.inputStream
            isConnected = true
            Log.d(TAG, "Successfully connected to Bluetooth device!")

            startWriterLoop()
            true
        } catch (e: Exception) {
            Log.e(TAG, "Failed to connect to Bluetooth device", e)
            close()
            false
        }
    }

    private fun startWriterLoop() {
        // Dedicated coroutine for critical commands (clicks, scrolls, shortcuts)
        scope.launch {
            for (packet in sendChannel) {
                val out = outputStream
                if (out != null && isConnected) {
                    try {
                        out.write(packet)
                        out.flush()
                    } catch (e: Exception) {
                        Log.e(TAG, "Error writing critical packet", e)
                        isConnected = false
                    }
                }
            }
        }

        // Dedicated coroutine for continuous high-frequency mouse movements
        scope.launch {
            for (packet in quatChannel) {
                val out = outputStream
                if (out != null && isConnected) {
                    try {
                        out.write(packet)
                        out.flush()
                    } catch (e: Exception) {
                        Log.e(TAG, "Error writing QUAT packet", e)
                        isConnected = false
                    }
                }
            }
        }
    }

    // Packet Serializers

    fun sendQuat(w: Float, x: Float, y: Float, z: Float) {
        val buffer = ByteBuffer.allocate(17).order(ByteOrder.LITTLE_ENDIAN)
        buffer.put(0x01.toByte())
        buffer.putFloat(w)
        buffer.putFloat(x)
        buffer.putFloat(y)
        buffer.putFloat(z)
        quatChannel.trySend(buffer.array())
    }

    fun sendClick(button: Char, action: String) {
        val btnByte = when (button) {
            'L' -> 1.toByte()
            'R' -> 2.toByte()
            'M' -> 3.toByte()
            else -> 1.toByte()
        }
        val actByte = if (action == "DOWN") 1.toByte() else 0.toByte()

        val buffer = ByteBuffer.allocate(3).order(ByteOrder.LITTLE_ENDIAN)
        buffer.put(0x02.toByte())
        buffer.put(btnByte)
        buffer.put(actByte)
        sendChannel.trySend(buffer.array())
    }

    fun sendScroll(delta: Int) {
        val buffer = ByteBuffer.allocate(3).order(ByteOrder.LITTLE_ENDIAN)
        buffer.put(0x03.toByte())
        buffer.putShort(delta.toShort())
        sendChannel.trySend(buffer.array())
    }

    fun sendSensitivity(sens: Float) {
        val buffer = ByteBuffer.allocate(5).order(ByteOrder.LITTLE_ENDIAN)
        buffer.put(0x04.toByte())
        buffer.putFloat(sens)
        sendChannel.trySend(buffer.array())
    }

    fun sendDoubleClick() {
        val buffer = byteArrayOf(0x05.toByte())
        sendChannel.trySend(buffer)
    }

    fun sendShortcut(action: String) {
        val shortcutId: Byte = when (action) {
            "ESC" -> 1.toByte()
            "CTRL_C" -> 2.toByte()
            "CTRL_V" -> 3.toByte()
            "CTRL_Z" -> 4.toByte()
            "ALT_TAB" -> 5.toByte()
            else -> 0.toByte()
        }
        val buffer = byteArrayOf(0x06.toByte(), shortcutId)
        sendChannel.trySend(buffer)
    }

    fun sendVolume(direction: String) {
        val dirByte: Byte = if (direction == "UP") 1.toByte() else (-1).toByte()
        val buffer = byteArrayOf(0x07.toByte(), dirByte)
        sendChannel.trySend(buffer)
    }

    fun sendDictation(text: String) {
        val bytes = text.toByteArray(Charsets.UTF_8)
        val buffer = ByteBuffer.allocate(3 + bytes.size).order(ByteOrder.LITTLE_ENDIAN)
        buffer.put(0x08.toByte())
        buffer.putShort(bytes.size.toShort())
        buffer.put(bytes)
        sendChannel.trySend(buffer.array())
    }

    fun sendRecenter() {
        val buffer = byteArrayOf(0x09.toByte())
        sendChannel.trySend(buffer)
    }

    suspend fun ping(): Boolean = withContext(Dispatchers.IO) {
        val out = outputStream
        val input = inputStream
        if (out != null && input != null && isConnected) {
            try {
                out.write(byteArrayOf(0xFF.toByte()))
                out.flush()
                true
            } catch (e: Exception) {
                isConnected = false
                false
            }
        } else {
            false
        }
    }

    fun close() {
        isConnected = false
        try {
            outputStream?.close()
            inputStream?.close()
            socket?.close()
        } catch (e: Exception) {
            Log.e(TAG, "Error closing Bluetooth socket", e)
        } finally {
            outputStream = null
            inputStream = null
            socket = null
        }
    }
}
