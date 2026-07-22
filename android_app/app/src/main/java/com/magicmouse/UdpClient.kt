package com.magicmouse

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import android.util.Log

object UdpClient {
    private const val TAG = "UdpClient"
    private var socket: DatagramSocket? = null
    private var pcAddress: InetAddress? = null
    private var pcPort: Int = 9876
    private val scope = CoroutineScope(Dispatchers.IO)
    // Use CONFLATED channel for continuous mouse movements to completely drop obsolete frames during Wi-Fi lag spikes!
    private val quatChannel = kotlinx.coroutines.channels.Channel<ByteArray>(kotlinx.coroutines.channels.Channel.CONFLATED)
    private val sendChannel = kotlinx.coroutines.channels.Channel<ByteArray>(kotlinx.coroutines.channels.Channel.UNLIMITED)

    fun initialize(ipAddress: String, port: Int) {
        scope.launch {
            try {
                if (socket == null || socket?.isClosed == true) {
                    socket = DatagramSocket()
                }
                pcAddress = InetAddress.getByName(ipAddress)
                pcPort = port
                Log.d(TAG, "Initialized UDP client pointing to $ipAddress:$port")
            } catch (e: Exception) {
                Log.e(TAG, "Initialization failed", e)
            }
        }
        
        // Dedicated coroutine to send critical packets (clicks, scrolls) sequentially
        scope.launch {
            for (bytes in sendChannel) {
                val s = socket
                val addr = pcAddress
                if (s != null && addr != null) {
                    try {
                        val packet = DatagramPacket(bytes, bytes.size, addr, pcPort)
                        s.send(packet)
                    } catch (e: Exception) {
                        Log.e(TAG, "Error sending packet", e)
                    }
                }
            }
        }

        // Dedicated coroutine to send mouse movements. Will automatically drop obsolete frames if network blocks!
        scope.launch {
            for (bytes in quatChannel) {
                val s = socket
                val addr = pcAddress
                if (s != null && addr != null) {
                    try {
                        val packet = DatagramPacket(bytes, bytes.size, addr, pcPort)
                        s.send(packet)
                    } catch (e: Exception) {
                        Log.e(TAG, "Error sending QUAT packet", e)
                    }
                }
            }
        }
    }

    fun sendQuat(w: Float, x: Float, y: Float, z: Float) {
        val buffer = java.nio.ByteBuffer.allocate(17).order(java.nio.ByteOrder.LITTLE_ENDIAN)
        buffer.put(0x01.toByte())
        buffer.putFloat(w)
        buffer.putFloat(x)
        buffer.putFloat(y)
        buffer.putFloat(z)
        quatChannel.trySend(buffer.array())
    }

    fun sendClick(button: String, action: String) {
        val btnByte = if (button == "L") 0x01.toByte() else 0x02.toByte()
        val actByte = if (action == "DOWN") 0x01.toByte() else 0x00.toByte()
        val buffer = byteArrayOf(0x02.toByte(), btnByte, actByte)
        sendChannel.trySend(buffer)
    }

    fun sendScroll(delta: Int) {
        val buffer = java.nio.ByteBuffer.allocate(3).order(java.nio.ByteOrder.LITTLE_ENDIAN)
        buffer.put(0x03.toByte())
        buffer.putShort(delta.toShort())
        sendChannel.trySend(buffer.array())
    }

    fun sendSensitivity(sens: Float) {
        val buffer = java.nio.ByteBuffer.allocate(5).order(java.nio.ByteOrder.LITTLE_ENDIAN)
        buffer.put(0x04.toByte())
        buffer.putFloat(sens)
        sendChannel.trySend(buffer.array())
    }

    fun sendDoubleClick() {
        val buffer = byteArrayOf(0x05.toByte())
        sendChannel.trySend(buffer)
    }

    fun sendShortcut(shortcutId: Int) {
        val buffer = byteArrayOf(0x06.toByte(), shortcutId.toByte())
        sendChannel.trySend(buffer)
    }

    fun sendVol(direction: Int) {
        val buffer = byteArrayOf(0x07.toByte(), direction.toByte())
        sendChannel.trySend(buffer)
    }

    fun sendDictation(text: String) {
        val bytes = text.toByteArray(Charsets.UTF_8)
        val buffer = java.nio.ByteBuffer.allocate(3 + bytes.size).order(java.nio.ByteOrder.LITTLE_ENDIAN)
        buffer.put(0x08.toByte())
        buffer.putShort(bytes.size.toShort())
        buffer.put(bytes)
        sendChannel.trySend(buffer.array())
    }

    fun sendRecenter() {
        val buffer = byteArrayOf(0x09.toByte())
        sendChannel.trySend(buffer)
    }

    fun send(message: String) {
        sendChannel.trySend(message.toByteArray())
    }

    suspend fun ping(ipAddress: String, port: Int): Boolean = withContext(Dispatchers.IO) {
        try {
            val tempSocket = DatagramSocket()
            tempSocket.soTimeout = 1500 // 1.5 second timeout
            val addr = InetAddress.getByName(ipAddress)
            val pingMsg = "PING".toByteArray()
            val sendPacket = DatagramPacket(pingMsg, pingMsg.size, addr, port)
            tempSocket.send(sendPacket)

            val buffer = ByteArray(1024)
            val receivePacket = DatagramPacket(buffer, buffer.size)
            tempSocket.receive(receivePacket)

            val response = String(receivePacket.data, 0, receivePacket.length)
            tempSocket.close()
            response == "PONG"
        } catch (e: Exception) {
            Log.e(TAG, "Ping failed to $ipAddress:$port", e)
            false
        }
    }

    fun close() {
        socket?.close()
        socket = null
        pcAddress = null
    }
}
