#ifndef MUCOM88_EDITOR_VOICE_APPEND_SERVICE_H
#define MUCOM88_EDITOR_VOICE_APPEND_SERVICE_H
#include "editor/text_transform_service.h"
#include "editor/mucom_compile_service.h"
#include "editor/voice_service.h"
namespace mucom88 {
class VoiceAppendService {
public:
    ServiceResult<TextTransformPreview> Preview(const DocumentSnapshot &source,
        const CompiledSong &song, const VoiceBankSnapshot &bank) const;
};
}
#endif
