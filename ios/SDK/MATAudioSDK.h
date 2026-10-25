#import "MATKeyAnalyzer.h"
#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
/** Shared parameters, C=0 ... Eb=3 ... A=9. Effect amounts0..1; tuning1 gives maximum mechanical
 * retune. Serialize lifecycle/parameter access on the main thread. Offline calls may run
 * independently on workers with a snapshot of parameters. */
@interface MATAudioParameters : NSObject <NSCopying>
@property (nonatomic) NSInteger tonic;
@property (nonatomic) NSInteger scale; // 0 major, 1 natural minor, 2 chromatic
@property (nonatomic) float tuning;
@property (nonatomic) float reverb;
@property (nonatomic) float echo;
@property (nonatomic) float denoise;
@property (nonatomic) float echoMs;
@property (nonatomic) float concertAHz;
/** -12..12 semitones, default0. Negative lowers the voice; positive raises it. */
@property (nonatomic) float pitchShiftSemitones;
@end
@interface MATAudioSDK : NSObject
- (nullable instancetype)initWithOffline:(BOOL)offline;
- (BOOL)setParameters:(MATAudioParameters *)parameters error:(NSError **)error;
/** Caller buffers contain normalized mono float PCM at 48kHz. Uses arbitrary-size
 * stream adapter. Do not call during live audio. Native frame C API also exported. */
- (BOOL)processInput:(const float *)input output:(float *)output frames:(NSInteger)frames;
- (void)reset;
@property (nonatomic, readonly) NSInteger latencySamples;
@property (nonatomic, readonly) BOOL running;
/** Includes actual I/O buffer and input/output latency estimates, plus
 * audioWorkgroupAvailable (iOS14+), callback P99 upper bound (0.1ms bins),
 * callback period/deadline overruns, processed sample count and timestamp gaps.
 * Timestamp gaps are not a hardware XRun counter. Estimates are not acoustic measurements. */
@property (nonatomic, readonly) NSDictionary<NSString *, NSNumber *> *telemetry;
/** RemoteIO duplex. Records input and processed output on a disk worker. Requires
 * mic permission. Rejects non-48kHz routes. SDK owns AVAudioSession while running.
 * Route changes/interruptions stop the stream; restart explicitly afterward. */
- (BOOL)startLiveRawURL:(NSURL *)rawURL
           processedURL:(NSURL *)processedURL
                monitor:(BOOL)monitor
                  error:(NSError **)error NS_SWIFT_NAME(startLive(raw:processed:monitor:));
- (void)stopLive;
/** Worker-thread API: PCM16/float32 mono/stereo 48kHz WAV, mono output.
 * Offline uses the high-quality model and larger pitch analysis window. */
+ (BOOL)processWavInputURL:(NSURL *)input
                 outputURL:(NSURL *)output
                parameters:(MATAudioParameters *)parameters
                   offline:(BOOL)offline
                appendTail:(BOOL)tail
                     error:(NSError **)error
    NS_SWIFT_NAME(processWav(input:output:parameters:offline:appendTail:));
@end
NS_ASSUME_NONNULL_END
