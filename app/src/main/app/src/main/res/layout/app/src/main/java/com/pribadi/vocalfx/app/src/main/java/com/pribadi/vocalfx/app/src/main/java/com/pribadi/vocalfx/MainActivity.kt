package com.pribadi.vocalfx

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.media.projection.MediaProjectionManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.Button
import android.widget.ProgressBar
import android.widget.SeekBar
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.SwitchCompat
import androidx.core.app.ActivityCompat

class MainActivity : AppCompatActivity() {

    external fun startAudioEngine()
    external fun stopAudioEngine()
    external fun setMixerLevels(mic: Float, music: Float)
    external fun setVocalEffects(gate: Float, comp: Float, harmMix: Float, harmPitch: Float, rev: Float)
    external fun setEqualizer(low: Float, mid: Float, high: Float)
    external fun setExtraFeatures(autoTune: Float, ducking: Boolean)
    external fun triggerSoundboard(sfxId: Int)
    external fun getVuMeterLevel(): Float
    external fun pushYoutubeAudio(data: FloatArray)

    private lateinit var projectionManager: MediaProjectionManager
    private var ytCapture: YoutubeAudioCapture? = null
    private val uiHandler = Handler(Looper.getMainLooper())
    private var currentHarmonyPitch = 1.2599f

    companion object {
        init { System.loadLibrary("vocalfx") }
    }

    private val screenCaptureLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK && result.data != null) {
            val serviceIntent = Intent(this, AudioCaptureService::class.java)
            startForegroundService(serviceIntent)

            uiHandler.postDelayed({
                val projection = projectionManager.getMediaProjection(result.resultCode, result.data!!)
                ytCapture = YoutubeAudioCapture(projection) { audioData ->
                    pushYoutubeAudio(audioData)
                }
                ytCapture?.startCapture()
                startAudioEngine()
            }, 250)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        projectionManager = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager

        ActivityCompat.requestPermissions(
            this,
            arrayOf(Manifest.permission.RECORD_AUDIO, Manifest.permission.POST_NOTIFICATIONS),
            100
        )

        findViewById<Button>(R.id.btnStartCapture).setOnClickListener {
            screenCaptureLauncher.launch(projectionManager.createScreenCaptureIntent())
        }

        val vuBar = findViewById<ProgressBar>(R.id.vuMeterProgress)
        uiHandler.post(object : Runnable {
            override fun run() {
                vuBar.progress = (getVuMeterLevel() * 100).toInt()
                uiHandler.postDelayed(this, 30)
            }
        })

        setupControls()
    }

    private fun setupControls() {
        val mic = findViewById<SeekBar>(R.id.seekMic)
        val music = findViewById<SeekBar>(R.id.seekMusic)
        val autoTune = findViewById<SeekBar>(R.id.seekAutoTune)
        val harm = findViewById<SeekBar>(R.id.seekHarmony)
        val rev = findViewById<SeekBar>(R.id.seekReverb)
        val comp = findViewById<SeekBar>(R.id.seekComp)
        val gate = findViewById<SeekBar>(R.id.seekGate)
        val low = findViewById<SeekBar>(R.id.seekEqLow)
        val mid = findViewById<SeekBar>(R.id.seekEqMid)
        val high = findViewById<SeekBar>(R.id.seekEqHigh)
        val duckingSwitch = findViewById<SwitchCompat>(R.id.switchDucking)

        fun updateAllAudioParams() {
            setMixerLevels(mic.progress / 100f, music.progress / 100f)
            setVocalEffects(
                gate.progress / 1000f,
                comp.progress / 100f,
                harm.progress / 100f,
                currentHarmonyPitch,
                rev.progress / 100f
            )
            setEqualizer(low.progress / 50f, mid.progress / 50f, high.progress / 50f)
            setExtraFeatures(autoTune.progress / 100f, duckingSwitch.isChecked)
        }

        val listener = object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar?, p: Int, u: Boolean) { updateAllAudioParams() }
            override fun onStartTrackingTouch(s: SeekBar?) {}
            override fun onStopTrackingTouch(s: SeekBar?) {}
        }

        listOf(mic, music, autoTune, harm, rev, comp, gate, low, mid, high).forEach {
            it.setOnSeekBarChangeListener(listener)
        }

        duckingSwitch.setOnCheckedChangeListener { _, _ ->
            updateAllAudioParams()
        }

        findViewById<Button>(R.id.btnPresetPop).setOnClickListener {
            harm.progress = 0; rev.progress = 20; comp.progress = 55; low.progress = 45; high.progress = 60
            updateAllAudioParams()
        }
        findViewById<Button>(R.id.btnPresetHall).setOnClickListener {
            harm.progress = 0; rev.progress = 55; comp.progress = 50; low.progress = 50; high.progress = 55
            updateAllAudioParams()
        }
        findViewById<Button>(R.id.btnPresetHarmHigh).setOnClickListener {
            currentHarmonyPitch = 1.2599f
            harm.progress = 40; rev.progress = 30
            updateAllAudioParams()
        }
        findViewById<Button>(R.id.btnPresetHarmLow).setOnClickListener {
            currentHarmonyPitch = 0.5f
            harm.progress = 45; rev.progress = 25
            updateAllAudioParams()
        }

        findViewById<Button>(R.id.btnSfxClap).setOnClickListener { triggerSoundboard(1) }
        findViewById<Button>(R.id.btnSfxHorn).setOnClickListener { triggerSoundboard(2) }
        findViewById<Button>(R.id.btnSfxDing).setOnClickListener { triggerSoundboard(3) }
    }

    override fun onDestroy() {
        super.onDestroy()
        ytCapture?.stopCapture()
        stopAudioEngine()
        stopService(Intent(this, AudioCaptureService::class.java))
    }
}
