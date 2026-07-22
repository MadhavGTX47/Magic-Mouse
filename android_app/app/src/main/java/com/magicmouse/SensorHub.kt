package com.magicmouse

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Handler
import android.os.HandlerThread
import android.util.Log

class SensorHub(context: Context) : SensorEventListener {
    private val TAG = "SensorHub"
    private val sensorManager = context.getSystemService(Context.SENSOR_SERVICE) as SensorManager

    // Use ROTATION_VECTOR: Android's built-in Kalman-fused gyro + accelerometer + magnetometer.
    // This gives us absolute orientation as a quaternion, eliminating gyroscope drift entirely.
    private val rotationVectorSensor = sensorManager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR)

    var isTracking = false
    var sensitivity = 15.0f  // Degrees to pixels multiplier
    var connMode: String = "BT"

    // Reference quaternion captured on re-center.
    // All subsequent readings are computed relative to this reference.
    // Q_cursor = Q_reset_inverse * Q_current
    private var referenceQuaternion: FloatArray? = null  // [w, x, y, z]

    // Pre-allocated arrays for zero-allocation sensor loop (prevents GC freezes)
    private val currentQuat = FloatArray(4)
    private val refInverse = FloatArray(4)
    private val relativeQuat = FloatArray(4)

    // Callback interface to notify activity of calibration events
    var onRecenterComplete: (() -> Unit)? = null

    // Background thread for sensor processing to prevent UI thread stutter
    private var sensorThread: HandlerThread? = null
    private var sensorHandler: Handler? = null

    fun start() {
        // Request SENSOR_DELAY_FASTEST to match high-refresh rate displays dynamically (up to 240Hz)
        val sensorDelay = SensorManager.SENSOR_DELAY_FASTEST
        
        // Start background thread for zero-stutter sensor callbacks
        if (sensorThread == null) {
            sensorThread = HandlerThread("MagicMouseSensorThread").apply { start() }
            sensorHandler = Handler(sensorThread!!.looper)
        }

        if (rotationVectorSensor != null) {
            sensorManager.registerListener(this, rotationVectorSensor, sensorDelay, sensorHandler)
            Log.d(TAG, "Rotation vector sensor tracking started (9-axis fused, MAX Hz)")
        } else {
            Log.e(TAG, "Rotation vector sensor not available! Falling back to game rotation vector...")
            val gameRotation = sensorManager.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR)
            if (gameRotation != null) {
                sensorManager.registerListener(this, gameRotation, sensorDelay, sensorHandler)
                Log.d(TAG, "Game rotation vector sensor tracking started (6-axis fused, MAX Hz)")
            } else {
                Log.e(TAG, "No rotation vector sensors available on this device!")
            }
        }
    }

    fun stop() {
        sensorManager.unregisterListener(this)
        sensorThread?.quitSafely()
        sensorThread = null
        sensorHandler = null
        Log.d(TAG, "Sensor tracking stopped")
    }

    /**
     * Re-center: Captures the current orientation as the new "zero point".
     * All subsequent cursor positions will be relative to this orientation.
     * This is the key to making phone-based pointing work:
     * the phone knows its orientation relative to the Earth, but NOT relative
     * to your monitor. Re-centering bridges that gap.
     */
    fun recenter() {
        // The next sensor event will capture the reference.
        // We set referenceQuaternion to null to signal "capture on next reading".
        referenceQuaternion = null
        Log.d(TAG, "Re-center requested. Will capture reference on next sensor event.")
    }

    override fun onSensorChanged(event: SensorEvent?) {
        if (event == null) return
        if (event.sensor.type != Sensor.TYPE_ROTATION_VECTOR &&
            event.sensor.type != Sensor.TYPE_GAME_ROTATION_VECTOR) return

        // Android's rotation vector event gives us a unit quaternion [x, y, z, w] (note: w is last!)
        // We convert to our internal format [w, x, y, z] for standard quaternion math.
        currentQuat[0] = if (event.values.size > 3) event.values[3] else computeW(event.values) // w
        currentQuat[1] = event.values[0] // x
        currentQuat[2] = event.values[1] // y
        currentQuat[3] = event.values[2] // z

        // If no reference is set yet, capture the current orientation as reference
        if (referenceQuaternion == null) {
            referenceQuaternion = currentQuat.copyOf()
            Log.d(TAG, "Reference quaternion captured: [${currentQuat[0]}, ${currentQuat[1]}, ${currentQuat[2]}, ${currentQuat[3]}]")
            onRecenterComplete?.invoke()
            return
        }

        if (!isTracking) return

        // Compute relative quaternion: Q_relative = Q_ref_inverse * Q_current
        // This gives us orientation RELATIVE to where the user was pointing when they re-centered.
        quaternionInverse(referenceQuaternion!!, refInverse)
        quaternionMultiply(refInverse, currentQuat, relativeQuat)

        // Send the relative quaternion to the PC server via Bluetooth or Wi-Fi
        if (connMode == "WIFI") {
            UdpClient.sendQuat(relativeQuat[0], relativeQuat[1], relativeQuat[2], relativeQuat[3])
        } else {
            BluetoothClient.sendQuat(relativeQuat[0], relativeQuat[1], relativeQuat[2], relativeQuat[3])
        }
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {
        // Could log accuracy changes for debugging magnetometer interference
    }

    // --- Quaternion Math Utilities ---

    /**
     * Compute w component when Android omits it.
     * For a unit quaternion: w = sqrt(1 - x² - y² - z²)
     */
    private fun computeW(values: FloatArray): Float {
        val x = values[0]; val y = values[1]; val z = values[2]
        val wSquared = 1.0f - (x * x + y * y + z * z)
        return if (wSquared > 0) Math.sqrt(wSquared.toDouble()).toFloat() else 0.0f
    }

    /**
     * Quaternion inverse (conjugate for unit quaternions).
     * q^-1 = [w, -x, -y, -z] / |q|²
     * For unit quaternions |q|=1, so q^-1 = [w, -x, -y, -z]
     */
    private fun quaternionInverse(q: FloatArray, out: FloatArray) {
        out[0] = q[0]
        out[1] = -q[1]
        out[2] = -q[2]
        out[3] = -q[3]
    }

    /**
     * Hamilton product: q1 * q2
     * Standard quaternion multiplication formula.
     */
    private fun quaternionMultiply(q1: FloatArray, q2: FloatArray, out: FloatArray) {
        val w1 = q1[0]; val x1 = q1[1]; val y1 = q1[2]; val z1 = q1[3]
        val w2 = q2[0]; val x2 = q2[1]; val y2 = q2[2]; val z2 = q2[3]
        out[0] = w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2 // w
        out[1] = w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2 // x
        out[2] = w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2 // y
        out[3] = w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2 // z
    }
}
