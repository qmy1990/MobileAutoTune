#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, MATKeyExecution) {
    MATKeyExecutionAutomatic = 0,
    MATKeyExecutionCPU = 1,
    MATKeyExecutionAccelerated = 2,
};
typedef NS_ENUM(NSInteger, MATKeyAnalysisStatus) {
    MATKeyAnalysisStatusEstimated = 0,
    MATKeyAnalysisStatusAmbiguous = 1,
    MATKeyAnalysisStatusInsufficientMelody = 2,
};

@interface MATKeyAnalysisOptions : NSObject <NSCopying>
@property (nonatomic) NSInteger threads;
@property (nonatomic) MATKeyExecution execution;
@property (nonatomic) BOOL detectModulations;
@end

@interface MATKeyCandidate : NSObject
@property (nonatomic, readonly) NSInteger tonic;
@property (nonatomic, readonly) NSInteger scale;
/** Relative ranking weight, not calibrated probability of correctness. */
@property (nonatomic, readonly) double support;
@property (nonatomic, readonly) double scaleCoverage;
@end

@interface MATKeySegment : NSObject
@property (nonatomic, readonly) double startSeconds;
@property (nonatomic, readonly) double endSeconds;
@property (nonatomic, readonly) NSInteger tonic;
@property (nonatomic, readonly) NSInteger scale;
@property (nonatomic, readonly) double support;
@end

@interface MATKeyAnalysisResult : NSObject
@property (nonatomic, readonly) MATKeyAnalysisStatus status;
@property (nonatomic, readonly, copy) NSArray<MATKeyCandidate *> *candidates;
@property (nonatomic, readonly, copy) NSArray<MATKeySegment *> *segments;
@property (nonatomic, readonly) BOOL possibleModulation;
@property (nonatomic, readonly) double durationSeconds;
@property (nonatomic, readonly) double voicedSeconds;
@property (nonatomic, readonly) double tuningCents;
@property (nonatomic, readonly) double modelAgreement;
@property (nonatomic, readonly) double elapsedSeconds;
@property (nonatomic, readonly) NSInteger noteCount;
@property (nonatomic, readonly) NSInteger distinctPitchClasses;
@property (nonatomic, readonly) NSInteger threads;
@property (nonatomic, readonly, copy) NSString *swiftBackend;
@property (nonatomic, readonly, copy) NSString *rmvpeBackend;
@property (nonatomic, readonly, copy) NSString *JSON;
@end

/** Called on the worker thread. Return NO to cancel. */
typedef BOOL (^MATKeyAnalysisProgress)(float progress);

/** Offline, local FP32 SwiftF0/RMVPE analysis. Use a worker thread for all
 * methods except cancel. One active analysis per instance. Full model assets
 * are supplied separately from the SDK library. No network access is used. */
@interface MATKeyAnalyzer : NSObject
- (nullable instancetype)initWithModelDirectory:(NSURL *)directory
                                        options:(MATKeyAnalysisOptions *)options
                                          error:(NSError **)error;
+ (nullable NSURL *)bundledModelDirectory;
- (nullable MATKeyAnalysisResult *)analyzeAudioURL:(NSURL *)audioURL
                                          progress:(nullable MATKeyAnalysisProgress)progress
                                             error:(NSError **)error
    NS_SWIFT_NAME(analyze(audio:progress:));
- (void)cancel;
@end

NS_ASSUME_NONNULL_END
