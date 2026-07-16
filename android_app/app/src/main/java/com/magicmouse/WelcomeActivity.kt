package com.magicmouse

import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.os.Bundle
import android.view.View
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import com.magicmouse.databinding.ActivityWelcomeBinding
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class WelcomeActivity : AppCompatActivity() {

    private lateinit var binding: ActivityWelcomeBinding
    private lateinit var sharedPreferences: SharedPreferences

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityWelcomeBinding.inflate(layoutInflater)
        setContentView(binding.root)

        sharedPreferences = getSharedPreferences("MagicMousePrefs", Context.MODE_PRIVATE)

        animateEntrance()

        // Load saved values
        val savedIp = sharedPreferences.getString("pc_ip", "")
        val savedPort = sharedPreferences.getInt("pc_port", 9876)

        binding.ipEditText.setText(savedIp)
        binding.portEditText.setText(savedPort.toString())

        binding.connectButton.setOnClickListener {
            val ip = binding.ipEditText.text.toString().trim()
            val portStr = binding.portEditText.text.toString().trim()

            if (ip.isEmpty()) {
                binding.ipInputLayout.error = "IP Address is required"
                return@setOnClickListener
            }
            binding.ipInputLayout.error = null

            val port = portStr.toIntOrNull() ?: 9876

            // Start connection test
            testConnection(ip, port)
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

    private fun testConnection(ip: String, port: Int) {
        binding.connectButton.isEnabled = false
        binding.statusText.visibility = View.VISIBLE
        binding.statusText.text = getString(R.string.connecting)
        binding.statusText.setTextColor(getColor(R.color.text_secondary))

        lifecycleScope.launch(Dispatchers.IO) {
            val isSuccess = UdpClient.ping(ip, port)

            withContext(Dispatchers.Main) {
                binding.connectButton.isEnabled = true
                if (isSuccess) {
                    binding.statusText.text = "Connected!"
                    binding.statusText.setTextColor(getColor(R.color.cyan_accent))

                    // Save settings
                    sharedPreferences.edit().apply {
                        putString("pc_ip", ip)
                        putInt("pc_port", port)
                        apply()
                    }

                    // Go to MouseActivity
                    val intent = Intent(this@WelcomeActivity, MouseActivity::class.java).apply {
                        putExtra("PC_IP", ip)
                        putExtra("PC_PORT", port)
                    }
                    startActivity(intent)
                    finish()
                } else {
                    binding.statusText.text = getString(R.string.connection_failed)
                    binding.statusText.setTextColor(getColor(android.R.color.holo_red_light))
                }
            }
        }
    }
}
