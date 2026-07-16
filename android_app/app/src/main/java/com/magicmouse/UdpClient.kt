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
    // Use UNLIMITED channel: We must not drop packets! Dropping packets causes clicks to disappear
    // and causes jerky tracking. We already fixed the underlying freezes via HandlerThread.
    private val sendChannel = kotlinx.coroutines.channels.Channel<String>(kotlinx.coroutines.channels.Channel.UNLIMITED)
    
    // Use CONFLATED channel for continuous mouse movements to completely drop obsolete frames during Wi-Fi lag spikes!
    private val quatChannel = kotlinx.coroutines.channels.Channel<String>(kotlinx.coroutines.channels.Channel.CONFLATED)

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
            for (message in sendChannel) {
                val s = socket
                val addr = pcAddress
                if (s != null && addr != null) {
                    try {
                        val bytes = message.toByteArray()
                        val packet = DatagramPacket(bytes, bytes.size, addr, pcPort)
                        s.send(packet)
                    } catch (e: Exception) {
                        Log.e(TAG, "Error sending packet: $message", e)
                    }
                }
            }
        }

        // Dedicated coroutine to send mouse movements. Will automatically drop obsolete frames if network blocks!
        scope.launch {
            for (message in quatChannel) {
                val s = socket
                val addr = pcAddress
                if (s != null && addr != null) {
                    try {
                        val bytes = message.toByteArray()
                        val packet = DatagramPacket(bytes, bytes.size, addr, pcPort)
                        s.send(packet)
                    } catch (e: Exception) {
                        Log.e(TAG, "Error sending QUAT packet", e)
                    }
                }
            }
        }
    }

    fun send(message: String) {
        // Offer message to the appropriate channel immediately, non-blocking
        if (message.startsWith("QUAT:")) {
            quatChannel.trySend(message)
        } else {
            val result = sendChannel.trySend(message)
            if (result.isFailure) {
                Log.e(TAG, "Failed to queue packet: $message")
            }
        }
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
