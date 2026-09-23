// MucomModule 
// BouKiCHi 2019

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mucom_module.h"

#define MUCOM_DEFAULT_PCMFILE "mucompcm.bin"
#define DEFAULT_OUTFILE "mucom88.mub"

MucomModule::MucomModule() {
  audioRate = 44100;
  pcmfile = MUCOM_DEFAULT_PCMFILE;
  outfile = DEFAULT_OUTFILE;
  voicefile = NULL;
  mucom = NULL;
  resultText = NULL;
  volume = 1.0f;
}

MucomModule::~MucomModule() {
  Close();
}

void MucomModule::SetRate(int rate) {
  audioRate = rate;
}

void MucomModule::SetPCM(const char *file) {
  pcmfile = file;
}

void MucomModule::SetVoice(const char *file) {
  voicefile = file;
}

void MucomModule::SetVolume(double vol) {
  volume = vol;
}


bool MucomModule::Open(const char *workingDirectory, const char *songFilename) {
  if (workingDirectory == NULL || songFilename == NULL ||
      chdir(workingDirectory) != 0) {
    AddResultBuffer("#Unable to enter the song directory.\r\n");
    return false;
  }
  mucom = new CMucom();
  const int compileOptions = MUCOM_CMPOPT_COMPILE;
  // CMucom::Init() accepts VM options, not compiler options.  Keep the
  // compiler VM in step mode so opening a source file does not create a
  // second SDL audio device and timer behind miniplay's AudioSdl instance.
  if (!mucom->Init(NULL, MUCOM_OPTION_STEP, audioRate)) {
    AddResultBuffer("#Unable to initialize MUCOM for compilation.\r\n");
    FreeMucom();
    return false;
  }
  mucom->Reset(compileOptions);
  if (pcmfile && mucom->LoadPCM(pcmfile) != 0) {
    AddResultBuffer(GetMucomMessage());
    FreeMucom();
    return false;
  }
  if (voicefile && mucom->LoadFMVoice(voicefile) != 0) {
    AddResultBuffer(GetMucomMessage());
    FreeMucom();
    return false;
  }
  int cr = mucom->CompileFile(songFilename, outfile);

  AddResultBuffer(GetMucomMessage());
  FreeMucom();

  if (cr != 0) return false; 

  // 再生用に再度準備
  mucom = new CMucom();
  if (!mucom->Init(NULL, MUCOM_OPTION_STEP, audioRate)) {
    AddResultBuffer("#Unable to initialize MUCOM for playback.\r\n");
    FreeMucom();
    return false;
  }
  mucom->Reset(0);
  if (mucom->LoadMusic(outfile) < 0) {
      AddResultBuffer(GetMucomMessage());
    return false;
  }
  return true;
}

const char *MucomModule::GetMucomMessage() {
  mucom->PrintInfoBuffer();
  return mucom->GetMessageBuffer();
}

void MucomModule::AddResultBuffer(const char *text) {
  int len = strlen(text);
  if (resultText != NULL) len += strlen(resultText);
  char *ptr = new char[len+1];
  if (resultText != NULL) strcpy(ptr, resultText); else  ptr[0] = 0x00;
  strcat(ptr, text);
  FreeResultBuffer();
  resultText = ptr;
}

bool MucomModule::Play() {
    if (!mucom) return false;
    if (mucom->Play(0) < 0) return false;
    return true;
}


void MucomModule::Close() {
  FreeMucom();
  FreeResultBuffer();
}

void MucomModule::FreeMucom() {
  if (!mucom) return;
  delete mucom;
  mucom = NULL;
}

void MucomModule::FreeResultBuffer() {
  if (!resultText) return;
  delete[] resultText;
  resultText = NULL;
}

const char *MucomModule::GetResult() {
  if (!resultText) return "";
  return resultText;
}

void MucomModule::Mix(short *data, int samples) {
	int buf[128];

  int index = 0;
  while(samples > 0) {
      int s = samples < 16 ? samples : 16;
      mucom->RenderAudio(buf, s);

      for(int i=0; i < s*2; i++) {
          int v=(buf[i]*volume);

          data[index] = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
          index++;
      }
      samples -= s;
  }
}
