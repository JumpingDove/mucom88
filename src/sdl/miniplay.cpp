#include <stdio.h>
#include <SDL.h>
#include <csignal>
#include "audiosdl.h"
#include "mucom_module.h"

namespace {

volatile std::sig_atomic_t BreakRequested = 0;

void SignalHandler(int)
{
    BreakRequested = 1;
}

} // namespace

class Player {
public:
    Player();
    ~Player();
    int Play(const char *filename);
    void Stop();

    void AudioCallback(void *mix, int size);

    AudioSdl *sdl;
    MucomModule *module;
private:
    bool EventCheck();
};

Player::Player() : sdl(NULL), module(NULL) {
}

Player::~Player() {
    Stop();
}

bool Player::EventCheck() {
    if (BreakRequested != 0) return true;

    SDL_Event evt;

    while(SDL_PollEvent(&evt)) {
        switch(evt.type) {
        case SDL_QUIT:
            return true;
        break;
        }
    }
    return false;
}

// オーディオコールバック
static void RunAudioCallback(void *CallbackInstance,void *MethodInstance);

static void RunAudioCallback(void *CallbackInstance,void *MethodInstance) {
    AudioCallback *acb = (AudioCallback *)CallbackInstance;
    ((Player *)MethodInstance)->AudioCallback(acb->mix, acb->size);
}

// オーディオ処理
void Player::AudioCallback(void *mix, int size) {
    module->Mix((short*)mix, size);
}

int Player::Play(const char *filename) {
    Stop();
    sdl = new AudioSdl();
    sdl->UserAudioCallback->Set(this,RunAudioCallback);
    module = new MucomModule();

    printf("File:%s\n", filename);
    bool r = module->Open(".", filename);
    puts(module->GetResult());
    if (!r) return -1;
    r = module->Play();
    if (!r) return -1;

    if (!sdl->Open(44100)) return -1;

    // イベントループ
    printf("Playing..\n");
    while(1) {
        if (EventCheck()) break;
        SDL_Delay(20);
    }
    return 0;
}

void Player::Stop() {
    if (sdl != NULL) {
        sdl->Close();
        delete sdl;
        sdl = NULL;
    }

    if (module != NULL) {
        module->Close();
        delete module;
        module = NULL;
    }
}


int main(int argc,char *argv[]) {
#if defined(USE_SDL) && defined(_WIN32)
    freopen("CON", "w", stdout);
    freopen("CON", "w", stderr );
#endif
    printf("MUCOM88 miniplay\n");
    if (argc < 2) {
        printf("usage miniplay <song.muc>\n");
        return 0;
    }
    BreakRequested = 0;
    if (std::signal(SIGINT, SignalHandler) == SIG_ERR ||
        std::signal(SIGTERM, SignalHandler) == SIG_ERR) {
        std::fprintf(stderr, "Failed to install signal handlers.\n");
        return 1;
    }

    Player player;
    return player.Play(argv[1]) == 0 ? 0 : 1;
}
