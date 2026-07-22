package com.magicmouse

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.View
import android.widget.ArrayAdapter
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.lifecycle.lifecycleScope
import com.magicmouse.databinding.ActivityWelcomeBinding
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class WelcomeActivity : AppCompatActivity() {

    private lateinit var binding: ActivityWelcomeBinding
    private lateinit var sharedPreferences: SharedPreferences
    private val pairedDevices = mutableListOf<BluetoothDevice>()
    private var selectedDevice: BluetoothDevice? = null

    companion object {
        private const val PERMISSION_REQUEST_CODE = 200
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityWelcomeBinding.inflate(layoutInflater)
        setContentView(binding.root)

        sharedPreferences = getSharedPreferences("MagicMousePrefs", Context.MODE_PRIVATE)

        animateEntrance()

        checkAndRequestPermissions()

        binding.refreshButton.setOnClickListener {
            checkAndRequestPermissions()
        }

        binding.connectButton.setOnClickListener {
            val device = selectedDevice
            if (device == null) {
                binding.deviceInputLayout.error = "Please select a paired PC"
                return@setOnClickListener
            }
            binding.deviceInputLayout.error = null

            connectToBluetoothDevice(device)
        }
    }

    private fun checkAndRequestPermissions() {
        val permissions = mutableListOf<String>()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
                permissions.add(Manifest.permission.BLUETOOTH_CONNECT)
            }
            if (checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) != PackageManager.PERMISSION_GRANTED) {
                permissions.add(Manifest.permission.BLUETOOTH_SCAN)
            }
        } else {
            if (checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
                permissions.add(Manifest.permission.ACCESS_FINE_LOCATION)
            }
        }

        if (permissions.isNotEmpty()) {
            ActivityCompat.requestPermissions(this, permissions.toTypedArray(), PERMISSION_REQUEST_CODE)
        } else {
            loadPairedDevices()
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == PERMISSION_REQUEST_CODE) {
            if (grantResults.isNotEmpty() && grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
                loadPairedDevices()
            } else {
                Toast.makeText(this, "Bluetooth permissions are required to connect", Toast.LENGTH_LONG).show()
            }
        }
    }

    @SuppressLint("MissingPermission")
    private fun loadPairedDevices() {
        val bluetoothAdapter = BluetoothAdapter.getDefaultAdapter()
        if (bluetoothAdapter == null) {
            Toast.makeText(this, "Bluetooth is not supported on this device", Toast.LENGTH_LONG).show()
            return
        }

        if (!bluetoothAdapter.isEnabled) {
            Toast.makeText(this, "Please turn on Bluetooth", Toast.LENGTH_LONG).show()
            return
        }

        val bonded = bluetoothAdapter.bondedDevices
        pairedDevices.clear()
        val deviceNames = mutableListOf<String>()

        bonded?.forEach { device ->
            pairedDevices.add(device)
            val name = device.name ?: "Unknown Device"
            deviceNames.add("$name (${device.address})")
        }

        if (deviceNames.isEmpty()) {
            deviceNames.add("No paired devices found. Pair PC in Android Settings first.")
        }

        val adapter = ArrayAdapter(this, android.R.layout.simple_dropdown_item_1line, deviceNames)
        binding.deviceAutoComplete.setAdapter(adapter)

        val lastAddress = sharedPreferences.getString("pc_bt_address", "")
        if (!lastAddress.isNullOrEmpty()) {
            val lastIdx = pairedDevices.indexOfFirst { it.address == lastAddress }
            if (lastIdx >= 0) {
                selectedDevice = pairedDevices[lastIdx]
                binding.deviceAutoComplete.setText(deviceNames[lastIdx], false)
            }
        }

        binding.deviceAutoComplete.setOnItemClickListener { _, _, position, _ ->
            if (position < pairedDevices.size) {
                selectedDevice = pairedDevices[position]
                binding.deviceInputLayout.error = null
            }
        }
    }

    private fun animateEntrance() {
        binding.topAppBar.alpha = 0f
        binding.topAppBar.translationY = -50f
        binding.topAppBar.animate().alpha(1f).translationY(0f).setDuration(800).start()
        
        binding.connectCard.alpha = 0f
        binding.connectCard.translationY = 50f
        binding.connectCard.animate().alpha(1f).translationY(0f).setDuration(800).setStartDelay(200).start()
    }

    @SuppressLint("MissingPermission")
    private fun connectToBluetoothDevice(device: BluetoothDevice) {
        binding.connectButton.isEnabled = false
        binding.statusText.visibility = View.VISIBLE
        binding.statusText.text = "Connecting via Bluetooth to ${device.name ?: device.address}..."
        binding.statusText.setTextColor(getColor(R.color.text_secondary))

        lifecycleScope.launch(Dispatchers.IO) {
            val isSuccess = BluetoothClient.connect(device)

            withContext(Dispatchers.Main) {
                binding.connectButton.isEnabled = true
                if (isSuccess) {
                    binding.statusText.text = "Connected via Bluetooth!"
                    binding.statusText.setTextColor(getColor(R.color.cyan_accent))

                    // Save settings
                    sharedPreferences.edit().apply {
                        putString("pc_bt_address", device.address)
                        putString("pc_bt_name", device.name ?: device.address)
                        apply()
                    }

                    // Go to MouseActivity
                    val intent = Intent(this@WelcomeActivity, MouseActivity::class.java).apply {
                        putExtra("BT_DEVICE_ADDRESS", device.address)
                        putExtra("BT_DEVICE_NAME", device.name ?: device.address)
                    }
                    startActivity(intent)
                    finish()
                } else {
                    binding.statusText.text = "Bluetooth connection failed! Ensure PC server is running."
                    binding.statusText.setTextColor(getColor(android.R.color.holo_red_light))
                }
            }
        }
    }
}
