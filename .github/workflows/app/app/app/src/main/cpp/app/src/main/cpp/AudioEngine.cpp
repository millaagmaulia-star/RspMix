#include <jni.h>
#include <oboe/Oboe.h>
#include <cmath>
#include <cstdlib>
#include <atomic>
#include <algorithm>

std::atomic<float> micVolume{1.0f};
std::atomic<float> musicVolume{0.7f};
std::atomic<float> vuLevel{0.0f};

std::atomic<float> gateThreshold{0.015f};
std::atomic<float> compAmount{0.5f};
std::atomic<float> eqLowGain{0.9f};
std::atomic<float> eqMidGain{1.0f};
std::atomic<float> eqHighGain{1.2f};
std::atomic<float> harmonyMix{0.0f};
std::atomic<float> harmonyPitch{1.2599f};
std::atomic<float> reverbMix{0.25f};

std::atomic<float> autoTuneAmount{0.0f};
std::atomic<bool> autoDucking{false};
std::atomic<int> activeSoundEffect{0};

constexpr int MAX_FRAMES = 2048;
float micReadBuffer[MAX_FRAMES] = {0.0f};

constexpr int DELAY_SIZE = 24000;
float delayBuffer[DELAY_SIZE] = {0.0f};
int delayIndex = 0;

constexpr int PITCH_BUF_SIZE = 4096;
float pitchBuffer[PITCH_BUF_SIZE] = {0.0f};
int pitchWriteIdx = 0;
float pitchReadIdx1 = 0.0f;
float pitchReadIdx2 = PITCH_BUF_SIZE / 2.0f;

constexpr int YT_BUFFER_SIZE = 96000;
float ytRingBuffer[YT_BUFFER_SIZE] = {0.0f};
std::atomic<int> ytWritePos{0};
std::atomic<int> ytReadPos{0};

float lowState = 0.0f;
float highState = 0.0f;
float envFollower = 0.0f;
int sfxSampleCounter = 0;
float sfxPhase = 0.0f;

float generateSoundboardSample() {
    int sfx = activeSoundEffect.load();
    if (sfx == 0) return 0.0f;

    float out = 0.0f;
    sfxSampleCounter++;

    if (sfx == 1) {
        if (sfxSampleCounter < 72000) {
            float noise = ((std::rand() % 2000) / 1000.0f) - 1.0f;
            float envelope = std::abs(std::sin(sfxSampleCounter * 0.0005f)) * (1.0f - (sfxSampleCounter / 72000.0f));
            out = noise * envelope * 0.4f;
        } else {
            activeSoundEffect.store(0);
            sfxSampleCounter = 0;
        }
    } else if (sfx == 2) {
        if (sfxSampleCounter < 38400) {
            sfxPhase += 440.0f / 48000.0f;
            if (sfxPhase > 1.0f) sfxPhase -= 1.0f;
            float wave = (sfxPhase * 2.0f - 1.0f) + (std::fmod(sfxPhase * 1.25f, 1.0f) * 2.0f - 1.0f);
            out = wave * 0.25f;
        } else {
            activeSoundEffect.store(0);
            sfxSampleCounter = 0;
        }
    } else if (sfx == 3) {
        if (sfxSampleCounter < 28800) {
            sfxPhase += 1046.5f / 48000.0f;
            float decay = std::exp(-sfxSampleCounter / 6000.0f);
            out = std::sin(sfxPhase * 2.0f * 3.14159f) * decay * 0.5f;
        } else {
            activeSoundEffect.store(0);
            sfxSampleCounter = 0;
        }
    }
    return out;
}

class VocalEngine : public oboe::AudioStreamDataCallback {
public:
    std::shared_ptr<oboe::AudioStream> outStream;
    std::shared_ptr<oboe::AudioStream> inStream;

    void start() {
        oboe::AudioStreamBuilder outBuilder;
        outBuilder.setDirection(oboe::Direction::Output)
                  ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
                  ->setSharingMode(oboe::SharingMode::Exclusive)
                  ->setFormat(oboe::AudioFormat::Float)
                  ->setChannelCount(oboe::ChannelCount::Stereo)
                  ->setDataCallback(this);

        if (outBuilder.openStream(outStream) != oboe::Result::OK) return;
        outStream->setBufferSizeInFrames(outStream->getFramesPerBurst() * 2);

        oboe::AudioStreamBuilder inBuilder;
        inBuilder.setDirection(oboe::Direction::Input)
                 ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
                 ->setSharingMode(oboe::SharingMode::Exclusive)
                 ->setFormat(oboe::AudioFormat::Float)
                 ->setChannelCount(oboe::ChannelCount::Mono)
                 ->setSampleRate(outStream->getSampleRate())
                 ->setInputPreset(oboe::InputPreset::VoicePerformance);

        if (inBuilder.openStream(inStream) != oboe::Result::OK) return;

        inStream->requestStart();
        outStream->requestStart();
    }

    void stop() {
        if (inStream) { inStream->stop(); inStream->close(); }
        if (outStream) { outStream->stop(); outStream->close(); }
    }

    float processHarmony(float inputSample, float pitchRatio) {
        pitchBuffer[pitchWriteIdx] = inputSample;
        
        int idx1 = static_cast<int>(pitchReadIdx1) % PITCH_BUF_SIZE;
        int idx2 = static_cast<int>(pitchReadIdx2) % PITCH_BUF_SIZE;
        
        float dist1 = std::abs(pitchReadIdx1 - pitchWriteIdx);
        if (dist1 > PITCH_BUF_SIZE / 2.0f) dist1 = PITCH_BUF_SIZE - dist1;
        float weight1 = std::min(1.0f, dist1 / (PITCH_BUF_SIZE / 4.0f));
        float weight2 = 1.0f - weight1;

        float harmonyOut = (pitchBuffer[idx1] * weight1) + (pitchBuffer[idx2] * weight2);

        pitchWriteIdx = (pitchWriteIdx + 1) % PITCH_BUF_SIZE;
        pitchReadIdx1 = std::fmod(pitchReadIdx1 + pitchRatio, static_cast<float>(PITCH_BUF_SIZE));
        pitchReadIdx2 = std::fmod(pitchReadIdx2 + pitchRatio, static_cast<float>(PITCH_BUF_SIZE));

        return harmonyOut;
    }

    oboe::DataCallbackResult onAudioReady(
            oboe::AudioStream *oboeStream,
            void *audioData,
            int32_t numFrames) override {

        float *outputBuffer = static_cast<float *>(audioData);
        int32_t framesToRead = std::min(numFrames, MAX_FRAMES);

        auto readResult = inStream->read(micReadBuffer, framesToRead, 0);
        int32_t framesRead = readResult ? readResult.value() : 0;

        float maxAmp = 0.0f;
        float gate = gateThreshold.load();
        float comp = compAmount.load();
        float harmMix = harmonyMix.load();
        float harmPitch = harmonyPitch.load();
        float rev = reverbMix.load();
        float tuneAmt = autoTuneAmount.load();
        bool ducking = autoDucking.load();

        for (int i = 0; i < numFrames; ++i) {
            float rawMic = (i < framesRead) ? micReadBuffer[i] : 0.0f;

            float absMic = std::abs(rawMic);
            envFollower = 0.99f * envFollower + 0.01f * absMic;
            if (envFollower < gate) {
                rawMic *= (envFollower / (gate + 0.0001f));
            }

            if (envFollower > 0.1f && comp > 0.0f) {
                float gainReduction = 1.0f / (1.0f + comp * (envFollower - 0.1f) * 4.0f);
                rawMic *= gainReduction * (1.0f + comp * 0.5f);
            }

            lowState += 0.05f * (rawMic - lowState);
            float lowBand = lowState;
            highState += 0.35f * (rawMic - highState);
            float highBand = rawMic - highState;
            float midBand = rawMic - lowBand - highBand;

            float eqVocal = (lowBand * eqLowGain.load()) +
                            (midBand * eqMidGain.load()) +
                            (highBand * eqHighGain.load());

            if (tuneAmt > 0.05f) {
                float quantized = std::round(eqVocal * 16.0f) / 16.0f;
                eqVocal = (eqVocal * (1.0f - tuneAmt * 0.4f)) + (quantized * (tuneAmt * 0.4f));
            }

            float harmonyVoice = 0.0f;
            if (harmMix > 0.01f) {
                harmonyVoice = processHarmony(eqVocal, harmPitch) * harmMix;
            }
            float vocalWithHarmony = eqVocal + harmonyVoice;

            float delayed = delayBuffer[delayIndex];
            float finalVocal = vocalWithHarmony + (delayed * rev);
            delayBuffer[delayIndex] = vocalWithHarmony + (delayed * 0.35f);
            delayIndex = (delayIndex + 1) % DELAY_SIZE;

            float ytSample = 0.0f;
            int rPos = ytReadPos.load();
            if (rPos != ytWritePos.load()) {
                ytSample = ytRingBuffer[rPos];
                ytReadPos.store((rPos + 1) % YT_BUFFER_SIZE);
            }

            float currentMusicVol = musicVolume.load();
            if (ducking && envFollower > 0.04f) {
                currentMusicVol *= 0.35f;
            }
            float sfxSample = generateSoundboardSample();

            float mixed = (finalVocal * micVolume.load()) + (ytSample * currentMusicVol) + sfxSample;
            mixed = std::tanh(mixed);

            float absVal = std::abs(mixed);
            if (absVal > maxAmp) maxAmp = absVal;

            outputBuffer[i * 2] = mixed;
            outputBuffer[i * 2 + 1] = mixed;
        }

        vuLevel.store(maxAmp);
        return oboe::DataCallbackResult::Continue;
    }
};

static VocalEngine engine;

extern "C" {
JNIEXPORT void JNICALL Java_com_pribadi_vocalfx_MainActivity_startAudioEngine(JNIEnv *, jobject) {
    engine.start();
}
JNIEXPORT void JNICALL Java_com_pribadi_vocalfx_MainActivity_stopAudioEngine(JNIEnv *, jobject) {
    engine.stop();
}
JNIEXPORT void JNICALL Java_com_pribadi_vocalfx_MainActivity_setMixerLevels(JNIEnv *, jobject, jfloat mic, jfloat music) {
    micVolume.store(mic);
    musicVolume.store(music);
}
JNIEXPORT void JNICALL Java_com_pribadi_vocalfx_MainActivity_setVocalEffects(JNIEnv *, jobject, jfloat gate, jfloat comp, jfloat harmMix, jfloat harmPitch, jfloat rev) {
    gateThreshold.store(gate);
    compAmount.store(comp);
    harmonyMix.store(harmMix);
    harmonyPitch.store(harmPitch);
    reverbMix.store(rev);
}
JNIEXPORT void JNICALL Java_com_pribadi_vocalfx_MainActivity_setEqualizer(JNIEnv *, jobject, jfloat low, jfloat mid, jfloat high) {
    eqLowGain.store(low);
    eqMidGain.store(mid);
    eqHighGain.store(high);
}
JNIEXPORT void JNICALL Java_com_pribadi_vocalfx_MainActivity_setExtraFeatures(JNIEnv *, jobject, jfloat autoTune, jboolean ducking) {
    autoTuneAmount.store(autoTune);
    autoDucking.store(ducking);
}
JNIEXPORT void JNICALL Java_com_pribadi_vocalfx_MainActivity_triggerSoundboard(JNIEnv *, jobject, jint sfxId) {
    sfxSampleCounter = 0;
    sfxPhase = 0.0f;
    activeSoundEffect.store(sfxId);
}
JNIEXPORT jfloat JNICALL Java_com_pribadi_vocalfx_MainActivity_getVuMeterLevel(JNIEnv *, jobject) {
    return vuLevel.load();
}
