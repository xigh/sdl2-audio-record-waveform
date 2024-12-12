// cc -I /opt/homebrew/include/ -o record/record record/record.c -L /opt/homebrew/lib -lSDL2

// whisperx ./audio.wav --compute_type int8 --model large-v3 --lang fr --batch_size 4

#include <SDL2/SDL.h>
#include <stdio.h>

#pragma pack(push, 1)
typedef struct {
    char chunk_id[4];     // "RIFF"
    uint32_t chunk_size;  // Size of entire file - 8
    char format[4];       // "WAVE"

    // Format sub-chunk
    char subchunk1_id[4];      // "fmt "
    uint32_t subchunk1_size;   // 16 for PCM
    uint16_t audio_format;     // 1 for PCM
    uint16_t num_channels;     // 2 for stereo
    uint32_t sample_rate;      // 44100
    uint32_t byte_rate;        // sample_rate * num_channels * bits_per_sample/8
    uint16_t block_align;      // num_channels * bits_per_sample/8
    uint16_t bits_per_sample;  // 16

    // Data sub-chunk
    char subchunk2_id[4];     // "data"
    uint32_t subchunk2_size;  // size of actual audio data
} WavHeader;
#pragma pack(pop)

FILE* audio_file = NULL;
long audio_data_size = 0;  // Add this to track data size
int16_t minV = INT16_MAX;
int16_t maxV = INT16_MIN;
Uint8* audio_buffer = NULL;
int audio_buffer_len = 0;

void audio_callback(void* userdata, Uint8* stream, int len) {
    if (audio_file) {
        // Store the audio data for visualization
        audio_buffer = realloc(audio_buffer, len);
        audio_buffer_len = len;
        memcpy(audio_buffer, stream, len);
        
        SDL_AudioSpec* spec = (SDL_AudioSpec*)userdata;

        switch (spec->format) {
            case AUDIO_S16LSB:
                fwrite(stream, 1, len, audio_file);
                for (int i = 0; i < len; i += 2) {
                    int16_t sample = SDL_SwapLE16(*(int16_t*)(stream + i));
                    if (sample < minV) minV = sample;
                    if (sample > maxV) maxV = sample;
                }
                audio_data_size += len;
                break;
            default:
                printf("Unsupported audio format: %d\n", spec->format);
                break;
        }
    }
}

void write_wav_header(FILE* file, int channels, int sample_rate, int bits_per_sample) {
    WavHeader header = {
        .chunk_id = {'R', 'I', 'F', 'F'},
        .format = {'W', 'A', 'V', 'E'},
        .subchunk1_id = {'f', 'm', 't', ' '},
        .subchunk1_size = 16,
        .audio_format = 1,
        .num_channels = SDL_SwapLE16(channels),
        .sample_rate = SDL_SwapLE32(sample_rate),
        .bits_per_sample = SDL_SwapLE16(bits_per_sample),
        .byte_rate = SDL_SwapLE32(sample_rate * channels * (bits_per_sample / 8)),
        .block_align = SDL_SwapLE16(channels * (bits_per_sample / 8)),
        .subchunk2_id = {'d', 'a', 't', 'a'}};

    fseek(file, 0, SEEK_SET);
    fwrite(&header, sizeof(header), 1, file);
}

int main(int argc, char* argv[]) {
    int res = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    if (res != 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    // enum audio devices
    int num_devices = SDL_GetNumAudioDevices(SDL_TRUE);
    printf("Number of recording devices: %d\n", num_devices);
    for (int i = 0; i < num_devices; i++) {
        printf("Audio device %d: %s\n", i, SDL_GetAudioDeviceName(i, SDL_TRUE));
    }

    if (num_devices == 0) {
        printf("No audio devices found\n");
        SDL_Quit();
        return 1;
    }

    SDL_bool start_recording = SDL_FALSE;

    SDL_AudioSpec obtained;
    SDL_AudioSpec desired;
    desired.freq = 44100;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 2048;
    desired.callback = audio_callback;
    desired.userdata = &obtained;

    SDL_AudioDeviceID device = SDL_OpenAudioDevice(NULL, SDL_TRUE, &desired, &obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (device == 0) {
        printf("SDL_OpenAudioDevice: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_AudioStatus status = SDL_GetAudioDeviceStatus(device);
    printf("SDL_AudioStatus: %d\n", status);

    // SDL_LockAudioDevice(device);

    char format_buf[100];
    snprintf(format_buf, sizeof(format_buf), "unknown 0x%x", obtained.format);
    char* spec_format = format_buf;
    switch (obtained.format) {
        case AUDIO_U8:
            spec_format = "AUDIO_U8";
            break;
        case AUDIO_S8:
            spec_format = "AUDIO_S8";
            break;
        case AUDIO_U16LSB:
            spec_format = "AUDIO_U16LSB";
            break;
        case AUDIO_S16LSB:
            spec_format = "AUDIO_S16LSB";
            break;
        case AUDIO_U16MSB:
            spec_format = "AUDIO_U16MSB";
            break;
        case AUDIO_S16MSB:
            spec_format = "AUDIO_S16MSB";
            break;
        case AUDIO_S32LSB:
            spec_format = "AUDIO_S32LSB";
            break;
        case AUDIO_S32MSB:
            spec_format = "AUDIO_S32MSB";
            break;
        case AUDIO_F32LSB:
            spec_format = "F32LSB";
            break;
        case AUDIO_F32MSB:
            spec_format = "F32MSB";
            break;
    }

    printf("Device %d: freq=%d, format=%s, channels=%d, samples=%d\n",
           device, obtained.freq, spec_format, obtained.channels, obtained.samples);

    // Start paused
    SDL_PauseAudioDevice(device, 1);

    int window_zoom = 2;
    int window_width = 320; 
    int window_height = 240 / 3;

    SDL_Window* window = SDL_CreateWindow("Hello, SDL2!", 
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
        window_width * window_zoom, window_height * window_zoom, 
        SDL_WINDOW_SHOWN);
    if (window == NULL) {
        printf("SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL) {
        printf("SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // removing the file if it exists
    remove("audio.wav");

    // set renderer scale
    SDL_RenderSetScale(renderer, window_zoom, window_zoom);

    for (;;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                goto cleanup;
            }

            // if escape key pressed, quit, if space key pressed, start recording
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    if (audio_file) {
                        // Update WAV header with final size
                        fseek(audio_file, 0, SEEK_SET);
                        uint32_t chunk_size = audio_data_size + sizeof(WavHeader) - 8;
                        fwrite("RIFF", 4, 1, audio_file);
                        fwrite(&chunk_size, 4, 1, audio_file);

                        fseek(audio_file, sizeof(WavHeader) - 4, SEEK_SET);
                        fwrite(&audio_data_size, 4, 1, audio_file);

                        fclose(audio_file);
                        audio_file = NULL;
                        audio_data_size = 0;
                    }
                    goto cleanup;
                }
                if (event.key.keysym.sym == SDLK_SPACE) {
                    if (SDL_GetAudioDeviceStatus(device) == SDL_AUDIO_PLAYING) {
                        printf("Pausing recording\n");
                        SDL_PauseAudioDevice(device, 1);
                        if (audio_file) {
                            // insert a silence of 1 second
                            int16_t silence[obtained.freq];
                            memset(silence, 0, sizeof(silence));
                            fwrite(silence, sizeof(silence), 1, audio_file);
                        }
                    } else {
                        if (!audio_file) {
                            audio_file = fopen("audio.wav", "wb");
                            if (!audio_file) {
                                printf("Failed to open audio.wav for writing\n");
                                continue;
                            }

                            write_wav_header(audio_file, obtained.channels, obtained.freq, 16);
                            fseek(audio_file, sizeof(WavHeader), SEEK_SET);
                        }
                        printf("Resuming recording\n");
                        SDL_PauseAudioDevice(device, 0);
                    }
                }
            }
        }

        if (audio_file) {
            static long prev_size = 0;

            const int N = 100000;
            if (prev_size / N != audio_data_size / N) {
                printf("Recording... %ld bytes, min=%d, max=%d\n", audio_data_size, minV, maxV);
                prev_size = audio_data_size;
            }
            SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 0, 100, 255, 255);
        }

        SDL_RenderClear(renderer);
        
        if (audio_file && SDL_GetAudioDeviceStatus(device) != SDL_AUDIO_PLAYING) {
            // draw a gradient from blue to red, vertically
            for (int y = 0; y < window_height; y++) {
                float t = (float)y / window_height;
                SDL_SetRenderDrawColor(renderer, (int)(255 * t), 0, (int)(255 * (1 - t)), 255);
                SDL_RenderDrawLine(renderer, 0, y, window_width, y);
            }
        } else {
            // draw the waveform
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            for (int x = 0; x < window_width; x++) {
                float t = (float)x / window_width;
                int y = (int)(window_height / 2);
                if (audio_buffer) {
                    float sample = ((int16_t*)audio_buffer)[x];
                    float norm = sample / 32768.0;
                    norm = norm * 7.0; // scale up
                    y += (int)(norm * window_height / 2);
                }
                SDL_RenderDrawPoint(renderer, x, y);
            }
        }

        SDL_RenderPresent(renderer);
    }

cleanup:
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (audio_file) {
        fclose(audio_file);
        audio_file = NULL;
    }
    if (audio_buffer) {
        free(audio_buffer);
        audio_buffer = NULL;
    }
    return 0;
}