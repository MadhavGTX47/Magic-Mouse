package com.magicmouse

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.content.res.ColorStateList
import android.os.Bundle
import android.speech.RecognitionListener
import android.speech.RecognizerIntent
import android.speech.SpeechRecognizer
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.widget.Button
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import android.util.Log
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.magicmouse.databinding.ActivityMouseBinding
import java.util.Locale
import android.animation.ObjectAnimator
import android.view.GestureDetector
import android.view.HapticFeedbackConstants
import kotlin.math.abs

class MouseActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMouseBinding
    private lateinit var sensorHub: SensorHub
    private lateinit var sharedPreferences: SharedPreferences
    
    private var speechRecognizer: SpeechRecognizer? = null
    private var isListeningSpeech = false
    private var pulseAnimator: ObjectAnimator? = null
    
    // For Scroll Area
    private var lastScrollY = 0f

    // For Double Tap
    private lateinit var leftClickGestureDetector: GestureDetector

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMouseBinding.inflate(layoutInflater)
        setContentView(binding.root)

        // Make fullscreen immersive
        window.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_FULLSCREEN
        )

        sharedPreferences = getSharedPreferences("MagicMousePrefs", Context.MODE_PRIVATE)

        val deviceName = intent.getStringExtra("BT_DEVICE_NAME") ?: "Bluetooth PC"

        binding.connectionStatus.text = "Connected via Bluetooth to $deviceName"

        sensorHub = SensorHub(this)
        
        // Load saved sensitivity
        val savedSensitivity = sharedPreferences.getFloat("sensitivity", 20.0f)
        sensorHub.sensitivity = savedSensitivity
        BluetoothClient.sendSensitivity(savedSensitivity)

        sensorHub.start()

        setupUI()
        animateWelcome()
        startHeartbeat(deviceName)
    }

    private fun startHeartbeat(deviceName: String) {
        lifecycleScope.launch {
            while (true) {
                val isConnected = BluetoothClient.ping()
                
                if (isConnected) {
                    binding.connectionStatus.text = "Connected via Bluetooth to $deviceName"
                    binding.connectionStatus.setTextColor(getColor(R.color.primary))
                } else {
                    binding.connectionStatus.text = "Bluetooth Disconnected! Trying to reconnect..."
                    binding.connectionStatus.setTextColor(getColor(android.R.color.holo_red_light))
                }
                delay(2000)
            }
        }
    }

    private fun animateWelcome() {
        binding.gyroToggleButtonCard.alpha = 0f
        binding.gyroToggleButtonCard.translationY = 50f
        binding.gyroToggleButtonCard.animate().alpha(1f).translationY(0f).setDuration(500).start()
        
        binding.clickLayout.alpha = 0f
        binding.clickLayout.translationY = -50f
        binding.clickLayout.animate().alpha(1f).translationY(0f).setDuration(500).start()
    }

    private fun applyButtonPressAnimation(view: View, isDown: Boolean) {
        val scale = if (isDown) 0.95f else 1.0f
        view.animate().scaleX(scale).scaleY(scale).setDuration(100).start()
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun setupUI() {
        // Gyro toggle
        binding.gyroToggleButtonCard.setOnClickListener {
            it.performHapticFeedback(HapticFeedbackConstants.CONTEXT_CLICK)
            sensorHub.isTracking = !sensorHub.isTracking
            if (sensorHub.isTracking) {
                sensorHub.recenter()
                BluetoothClient.sendRecenter()
            }
            updateGyroButtonUI()
        }

        // Re-center button
        binding.recenterButton.setOnClickListener {
            it.performHapticFeedback(HapticFeedbackConstants.CONTEXT_CLICK)
            sensorHub.recenter()
            BluetoothClient.sendRecenter()
            Toast.makeText(this, "Re-centered!", Toast.LENGTH_SHORT).show()
        }

        // Left Click (Double tap detection + Touch listener)
        leftClickGestureDetector = GestureDetector(this, object : GestureDetector.SimpleOnGestureListener() {
            override fun onDoubleTap(e: MotionEvent): Boolean {
                BluetoothClient.sendDoubleClick()
                binding.leftClickButton.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
                return true
            }
        })

        binding.leftClickButton.setOnTouchListener { v, event ->
            leftClickGestureDetector.onTouchEvent(event)
            handleMouseClick(v, event, 'L', R.color.button_pressed, R.color.surface_container)
            true
        }

        // Right Click
        binding.rightClickButton.setOnTouchListener { v, event ->
            handleMouseClick(v, event, 'R', R.color.button_pressed, R.color.surface_container)
            true
        }

        // Middle Click
        binding.middleClickButton.setOnTouchListener { v, event ->
            handleMouseClick(v, event, 'M', R.color.button_pressed, R.color.surface_container)
            true
        }

        // Scroll Area Logic (Vertical Dragging)
        binding.scrollArea.setOnTouchListener { _, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN -> {
                    lastScrollY = event.y
                }
                MotionEvent.ACTION_MOVE -> {
                    val dy = event.y - lastScrollY
                    if (abs(dy) > 40f) {
                        val scrollDelta = if (dy > 0) -1 else 1 // Swipe down = scroll up
                        BluetoothClient.sendScroll(scrollDelta)
                        lastScrollY = event.y
                    }
                }
            }
            true
        }

        // Mic FAB
        binding.micFab.setOnClickListener {
            it.performHapticFeedback(HapticFeedbackConstants.CONTEXT_CLICK)
            if (isListeningSpeech) stopSpeechRecognition() else startSpeechRecognition()
        }

        // Settings Button
        binding.settingsButton.setOnClickListener {
            showSettingsDialog()
        }
    }

    private fun handleMouseClick(v: View, event: MotionEvent, btn: Char, pressedColor: Int, normalColor: Int) {
        when (event.action) {
            MotionEvent.ACTION_DOWN -> {
                v.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
                BluetoothClient.sendClick(btn, "DOWN")
                v.backgroundTintList = ColorStateList.valueOf(getColor(pressedColor))
                applyButtonPressAnimation(v, true)
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                BluetoothClient.sendClick(btn, "UP")
                v.backgroundTintList = ColorStateList.valueOf(getColor(normalColor))
                applyButtonPressAnimation(v, false)
            }
        }
    }

    private fun updateGyroButtonUI() {
        if (sensorHub.isTracking) {
            binding.gyroStatusText.text = "TRACKING ACTIVE"
            binding.gyroStatusText.setTextColor(getColor(R.color.background))
            binding.gyroIcon.imageTintList = ColorStateList.valueOf(getColor(R.color.background))
            binding.gyroToggleButtonCard.setCardBackgroundColor(getColor(R.color.primary))
        } else {
            binding.gyroStatusText.text = "ACTIVATE GYRO"
            binding.gyroStatusText.setTextColor(getColor(R.color.primary))
            binding.gyroIcon.imageTintList = ColorStateList.valueOf(getColor(R.color.primary))
            binding.gyroToggleButtonCard.setCardBackgroundColor(getColor(R.color.surface_container))
        }
    }

    // Intercept physical volume buttons
    override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        return when (keyCode) {
            KeyEvent.KEYCODE_VOLUME_UP -> {
                BluetoothClient.sendVolume("UP")
                true
            }
            KeyEvent.KEYCODE_VOLUME_DOWN -> {
                BluetoothClient.sendVolume("DOWN")
                true
            }
            else -> super.onKeyDown(keyCode, event)
        }
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent?): Boolean {
        return when (keyCode) {
            KeyEvent.KEYCODE_VOLUME_UP, KeyEvent.KEYCODE_VOLUME_DOWN -> true
            else -> super.onKeyUp(keyCode, event)
        }
    }

    private fun startSpeechRecognition() {
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), 101)
            return
        }

        val intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
            putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
            putExtra(RecognizerIntent.EXTRA_LANGUAGE, Locale.getDefault())
        }

        speechRecognizer = SpeechRecognizer.createSpeechRecognizer(this).apply {
            setRecognitionListener(object : RecognitionListener {
                override fun onReadyForSpeech(params: Bundle?) {
                    isListeningSpeech = true
                    binding.micFab.setTextColor(getColor(android.R.color.holo_red_light))
                    Toast.makeText(this@MouseActivity, "Listening...", Toast.LENGTH_SHORT).show()
                }
                override fun onBeginningOfSpeech() {}
                override fun onRmsChanged(rmsdB: Float) {}
                override fun onBufferReceived(buffer: ByteArray?) {}
                override fun onEndOfSpeech() {
                    isListeningSpeech = false
                    binding.micFab.setTextColor(getColor(R.color.primary))
                }
                override fun onError(error: Int) {
                    isListeningSpeech = false
                    binding.micFab.setTextColor(getColor(R.color.primary))
                    Toast.makeText(this@MouseActivity, "Speech error: $error", Toast.LENGTH_SHORT).show()
                }
                override fun onResults(results: Bundle?) {
                    val matches = results?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)
                    if (!matches.isNullOrEmpty()) {
                        val text = matches[0]
                        BluetoothClient.sendDictation(text)
                        Toast.makeText(this@MouseActivity, "Typed: \"$text\"", Toast.LENGTH_SHORT).show()
                    }
                }
                override fun onPartialResults(partialResults: Bundle?) {}
                override fun onEvent(eventType: Int, params: Bundle?) {}
            })
        }
        speechRecognizer?.startListening(intent)
    }

    private fun stopSpeechRecognition() {
        speechRecognizer?.stopListening()
        isListeningSpeech = false
        binding.micFab.setTextColor(getColor(R.color.primary))
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == 101 && grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            startSpeechRecognition()
        } else {
            Toast.makeText(this, "Permission denied. Dictation disabled.", Toast.LENGTH_SHORT).show()
        }
    }

    private fun showSettingsDialog() {
        val dialog = BottomSheetDialog(this)
        val view = layoutInflater.inflate(R.layout.dialog_settings, null)
        dialog.setContentView(view)

        val sensitivityLabel = view.findViewById<TextView>(R.id.sensitivityLabelText)
        val seekBar = view.findViewById<SeekBar>(R.id.sensitivitySeekBar)
        val calibrateBtn = view.findViewById<Button>(R.id.calibrateButton)
        val disconnectBtn = view.findViewById<Button>(R.id.disconnectButton)

        // Setup Sensitivity SeekBar
        val currentProgress = sensorHub.sensitivity.toInt()
        seekBar.progress = currentProgress
        sensitivityLabel.text = "Sensitivity: $currentProgress"

        seekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                val cleanProgress = if (progress < 1) 1 else progress
                sensorHub.sensitivity = cleanProgress.toFloat()
                sensitivityLabel.text = "Sensitivity: $cleanProgress"
                BluetoothClient.sendSensitivity(cleanProgress.toFloat())
            }

            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {
                sharedPreferences.edit().putFloat("sensitivity", sensorHub.sensitivity).apply()
            }
        })

        // Setup Re-center Button
        calibrateBtn.setOnClickListener {
            sensorHub.recenter()
            BluetoothClient.sendRecenter()
            Toast.makeText(this, "Re-centered!", Toast.LENGTH_SHORT).show()
        }

        // Setup Disconnect Button
        disconnectBtn.setOnClickListener {
            dialog.dismiss()
            BluetoothClient.close()
            sensorHub.stop()
            
            val intent = Intent(this, WelcomeActivity::class.java)
            startActivity(intent)
            finish()
        }

        dialog.show()
    }

    override fun onDestroy() {
        super.onDestroy()
        pulseAnimator?.cancel()
        sensorHub.stop()
        BluetoothClient.close()
        speechRecognizer?.destroy()
    }
}
