#import "MATKeyAnalyzer.h"

#import <AVFoundation/AVFoundation.h>

#include <atomic>
#include <mutex>
#include <vector>

#include "mat/key_analysis.h"

static void MATKeyError(NSError **error, NSInteger code, NSString *message) {
    if (error)
        *error = [NSError errorWithDomain:@"MobileAutoTune.KeyAnalysis"
                                     code:code
                                 userInfo:@{NSLocalizedDescriptionKey : message}];
}

@implementation MATKeyAnalysisOptions
- (instancetype)init {
    if ((self = [super init])) {
        _execution = MATKeyExecutionAutomatic;
        _detectModulations = YES;
    }
    return self;
}
- (id)copyWithZone:(NSZone *)zone {
    MATKeyAnalysisOptions *copy = [[MATKeyAnalysisOptions allocWithZone:zone] init];
    copy.threads = _threads;
    copy.execution = _execution;
    copy.detectModulations = _detectModulations;
    return copy;
}
@end

@implementation MATKeyCandidate
- (instancetype)initWithCandidate:(const mat_key_candidate &)candidate {
    if ((self = [super init])) {
        _tonic = candidate.tonic;
        _scale = candidate.scale;
        _support = candidate.support;
        _scaleCoverage = candidate.scale_coverage;
    }
    return self;
}
@end

@implementation MATKeySegment
- (instancetype)initWithSegment:(const mat_key_segment &)segment {
    if ((self = [super init])) {
        _startSeconds = segment.start_seconds;
        _endSeconds = segment.end_seconds;
        _tonic = segment.tonic;
        _scale = segment.scale;
        _support = segment.support;
    }
    return self;
}
@end

@implementation MATKeyAnalysisResult
- (instancetype)initWithResult:(const mat_key_result &)result {
    if ((self = [super init])) {
        _status = static_cast<MATKeyAnalysisStatus>(result.status);
        _possibleModulation = result.possible_modulation;
        _durationSeconds = result.duration_seconds;
        _voicedSeconds = result.voiced_seconds;
        _tuningCents = result.tuning_cents;
        _modelAgreement = result.model_agreement;
        _elapsedSeconds = result.elapsed_seconds;
        _noteCount = result.note_count;
        _distinctPitchClasses = result.distinct_pitch_classes;
        _threads = result.threads;
        _swiftBackend = @(result.swift_backend);
        _rmvpeBackend = @(result.rmvpe_backend);
        NSMutableArray<MATKeyCandidate *> *keys = [NSMutableArray array];
        for (int i = 0; i < result.candidate_count; ++i)
            [keys addObject:[[MATKeyCandidate alloc] initWithCandidate:result.candidates[i]]];
        _candidates = [keys copy];
        NSMutableArray<MATKeySegment *> *windows = [NSMutableArray array];
        for (int i = 0; i < result.segment_count; ++i)
            [windows addObject:[[MATKeySegment alloc] initWithSegment:result.segments[i]]];
        _segments = [windows copy];
        std::vector<char> text(mat_key_result_json(&result, nullptr, 0));
        mat_key_result_json(&result, text.data(), text.size());
        _JSON = @(text.data());
    }
    return self;
}
@end

static int MATReportKeyProgress(float value, void *context) {
    MATKeyAnalysisProgress progress = (__bridge MATKeyAnalysisProgress)context;
    return progress(value) ? 0 : 1;
}

@implementation MATKeyAnalyzer {
    mat_key_analyzer *_analyzer;
    std::atomic<bool> _canceled;
    std::mutex _stateMutex;
    BOOL _analyzing;
}
- (instancetype)initWithModelDirectory:(NSURL *)directory
                               options:(MATKeyAnalysisOptions *)options
                                 error:(NSError **)error {
    if ((self = [super init])) {
        if (!directory.isFileURL || options.threads < 0 || options.threads > 64 ||
            options.execution < MATKeyExecutionAutomatic ||
            options.execution > MATKeyExecutionAccelerated) {
            MATKeyError(error, -1, @"Invalid key analysis options or model directory");
            return nil;
        }
        auto parameters = mat_default_key_options();
        parameters.threads = static_cast<int>(options.threads);
        parameters.execution = static_cast<mat_key_execution>(options.execution);
        parameters.detect_modulations = options.detectModulations;
        _analyzer = mat_key_create(directory.fileSystemRepresentation, &parameters);
        if (!_analyzer) {
            MATKeyError(error, -3, @"Cannot open the verified FP32 key model files");
            return nil;
        }
    }
    return self;
}
+ (NSURL *)bundledModelDirectory {
    return [NSBundle.mainBundle URLForResource:@"KeyModels" withExtension:@"bundle"];
}
- (void)cancel {
    std::lock_guard<std::mutex> lock(_stateMutex);
    _canceled = true;
    mat_key_cancel(_analyzer);
}
- (MATKeyAnalysisResult *)analyzeAudioURL:(NSURL *)audioURL
                                 progress:(MATKeyAnalysisProgress)progress
                                    error:(NSError **)error {
    {
        std::lock_guard<std::mutex> lock(_stateMutex);
        if (_analyzing || !_analyzer || !audioURL.isFileURL) {
            MATKeyError(error, -1, @"Analyzer is busy or the audio URL is invalid");
            return nil;
        }
        _analyzing = YES;
        _canceled = false;
    }
    NSDate *begin = NSDate.date;
    BOOL scoped = [audioURL startAccessingSecurityScopedResource];
    NSURL *temporary = [NSURL
        fileURLWithPath:[NSTemporaryDirectory()
                            stringByAppendingPathComponent:[NSUUID.UUID.UUIDString
                                                               stringByAppendingString:@".wav"]]];
    @try {
        if (progress && !progress(0))
            _canceled = true;
        AVAudioFile *source = [[AVAudioFile alloc] initForReading:audioURL
                                                     commonFormat:AVAudioPCMFormatFloat32
                                                      interleaved:NO
                                                            error:error];
        if (!source)
            return nil;
        AVAudioFormat *format = source.processingFormat;
        if (format.sampleRate < 8000 || format.sampleRate > 192000 || format.channelCount > 8 ||
            format.channelCount < 1 || source.length <= 0 ||
            source.length / format.sampleRate > 86400) {
            MATKeyError(error, -2, @"Unsupported audio rate, duration or channel count");
            return nil;
        }
        NSDictionary *settings = @{
            AVFormatIDKey : @(kAudioFormatLinearPCM),
            AVSampleRateKey : @(format.sampleRate),
            AVNumberOfChannelsKey : @(format.channelCount),
            AVLinearPCMBitDepthKey : @32,
            AVLinearPCMIsFloatKey : @YES,
            AVLinearPCMIsBigEndianKey : @NO,
            AVLinearPCMIsNonInterleaved : @NO
        };
        AVAudioFile *destination = [[AVAudioFile alloc] initForWriting:temporary
                                                              settings:settings
                                                          commonFormat:AVAudioPCMFormatFloat32
                                                           interleaved:NO
                                                                 error:error];
        if (!destination)
            return nil;
        AVAudioPCMBuffer *buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format
                                                                 frameCapacity:16384];
        if (!buffer) {
            MATKeyError(error, -4, @"Cannot allocate decoder buffer");
            return nil;
        }
        while (source.framePosition < source.length) {
            if (_canceled.load()) {
                MATKeyError(error, -5, @"Key analysis canceled");
                return nil;
            }
            if (![source readIntoBuffer:buffer error:error])
                return nil;
            if (!buffer.frameLength)
                break;
            if (![destination writeFromBuffer:buffer error:error])
                return nil;
            float fraction = .15 * double(source.framePosition) / source.length;
            if (progress && !progress(fraction))
                _canceled = true;
        }
        // Closing the writer finalizes the WAV data size before native reading.
        destination = nil;
        source = nil;
        MATKeyAnalysisProgress report = ^BOOL(float fraction) {
            if (self->_canceled.load())
                return NO;
            return !progress || progress(.15f + .85f * fraction);
        };
        mat_key_result result{};
        int code = mat_key_analyze_wav(_analyzer, temporary.fileSystemRepresentation, &result,
                                       MATReportKeyProgress, (__bridge void *)report);
        if (code) {
            MATKeyError(error, code, @(mat_key_last_error(_analyzer)));
            return nil;
        }
        result.elapsed_seconds = -begin.timeIntervalSinceNow;
        return [[MATKeyAnalysisResult alloc] initWithResult:result];
    } @finally {
        [NSFileManager.defaultManager removeItemAtURL:temporary error:nil];
        if (scoped)
            [audioURL stopAccessingSecurityScopedResource];
        std::lock_guard<std::mutex> lock(_stateMutex);
        _analyzing = NO;
    }
}
- (void)dealloc {
    mat_key_destroy(_analyzer);
}
@end
