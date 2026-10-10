import AVFoundation
import UIKit
import UniformTypeIdentifiers

@main
class AppDelegate: UIResponder, UIApplicationDelegate {
    var window: UIWindow?
    func application(
        _: UIApplication,
        didFinishLaunchingWithOptions _: [UIApplication.LaunchOptionsKey: Any]?
    ) -> Bool {
        let window = UIWindow(frame: UIScreen.main.bounds)
        window.rootViewController = DemoController()
        window.makeKeyAndVisible()
        self.window = window
        return true
    }
}

final class DemoController: UIViewController, UIPickerViewDataSource, UIPickerViewDelegate,
    UIDocumentPickerDelegate
{
    let parameters = MATAudioParameters()
    let sdk = MATAudioSDK(offline: false)!
    let keys = ["C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"]
    let status = UILabel(), meter = UILabel(), monitor = UISwitch(),
        record = UIButton(type: .system)
    var raw: URL?, wet: URL?, offline: URL?, player: AVAudioPlayer?, timer: Timer?
    var diagnosticSeconds = 0
    var diagnosticTimer: Timer?
    var diagnosticSamples: [[String: Any]] = []
    var diagnosticStart = Date()
    var diagnosticRoute: [String: Any] = [:]
    var busy = false
    var amountLabels: [UILabel] = []
    let pitchLabel = UILabel(), pitchSlider = UISlider()
    var directory: URL { FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0] }
    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground
        let scroll = UIScrollView()
        scroll.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(scroll)
        let stack = UIStackView()
        stack.axis = .vertical
        stack.spacing = 12
        stack.translatesAutoresizingMaskIntoConstraints = false
        scroll.addSubview(stack)
        NSLayoutConstraint.activate([
            scroll.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor),
            scroll.bottomAnchor.constraint(equalTo: view.bottomAnchor),
            scroll.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            scroll.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            stack.topAnchor.constraint(equalTo: scroll.contentLayoutGuide.topAnchor, constant: 20),
            stack.bottomAnchor.constraint(
                equalTo: scroll.contentLayoutGuide.bottomAnchor,
                constant: -24
            ),
            stack.leadingAnchor.constraint(
                equalTo: scroll.contentLayoutGuide.leadingAnchor,
                constant: 20
            ), stack.trailingAnchor.constraint(
                equalTo: scroll.contentLayoutGuide.trailingAnchor,
                constant: -20
            ),
            stack.widthAnchor.constraint(
                equalTo: scroll.frameLayoutGuide.widthAnchor,
                constant: -40
            ),
        ])
        let title = UILabel()
        title.text = "MobileAutoTune · 人声工作台"
        title.font = .boldSystemFont(ofSize: 25)
        title.adjustsFontSizeToFitWidth = true
        stack.addArrangedSubview(title)
        let hint = UILabel()
        hint.text = "48 kHz 人声，默认降E大调。开启监听时请使用有线/USB耳机；蓝牙不用于50ms验收。"
        hint.numberOfLines = 0
        hint.textColor = .secondaryLabel
        stack.addArrangedSubview(hint)
        let picker = UIPickerView()
        picker.dataSource = self
        picker.delegate = self
        picker.selectRow(3, inComponent: 0, animated: false)
        picker.heightAnchor.constraint(equalToConstant: 88).isActive = true
        stack.addArrangedSubview(picker)
        let scale = UISegmentedControl(items: ["大调", "自然小调", "半音阶"])
        scale.selectedSegmentIndex = 0
        scale.addTarget(self, action: #selector(scaleChanged(_:)), for: .valueChanged)
        stack.addArrangedSubview(scale)
        let names = ["修音（高强度电音）", "混响", "回声", "降噪（唱歌建议适中）"]
        let values: [Float] = [
            parameters.tuning,
            parameters.reverb,
            parameters.echo,
            parameters.denoise,
        ]
        for i in 0 ..< 4 {
            let label = UILabel()
            label.text = "\(names[i]) \(Int(values[i] * 100))%"
            amountLabels.append(label)
            stack.addArrangedSubview(label)
            let slider = UISlider()
            slider.tag = i
            slider.minimumValue = 0
            slider.maximumValue = 1
            slider.value = values[i]
            slider.accessibilityLabel = names[i]
            slider.addTarget(self, action: #selector(amountChanged(_:)), for: .valueChanged)
            stack.addArrangedSubview(slider)
        }
        pitchLabel.text = "音高（低沉 ↔ 高尖） 0.0 半音"
        stack.addArrangedSubview(pitchLabel)
        pitchSlider.minimumValue = -12
        pitchSlider.maximumValue = 12
        pitchSlider.value = 0
        pitchSlider.accessibilityLabel = "音高，负值低沉，正值高尖"
        pitchSlider.addTarget(self, action: #selector(pitchChanged(_:)), for: .valueChanged)
        stack.addArrangedSubview(pitchSlider)
        let resetPitchButton = UIButton(type: .system)
        resetPitchButton.setTitle("恢复原音高", for: .normal)
        resetPitchButton.addTarget(self, action: #selector(resetPitch), for: .touchUpInside)
        stack.addArrangedSubview(resetPitchButton)
        let row = UIStackView()
        let label = UILabel()
        label.text = "开启实时耳机监听"
        row.addArrangedSubview(label)
        row.addArrangedSubview(monitor)
        stack.addArrangedSubview(row)
        record.setTitle("开始录音", for: .normal)
        record.tag = 0
        record.addTarget(self, action: #selector(buttonPressed(_:)), for: .touchUpInside)
        stack.addArrangedSubview(record)
        for (index, name) in ["导入48kHz WAV", "高质量后处理", "试听原音", "试听实时效果录音", "试听后处理效果", "分享后处理WAV"]
            .enumerated()
        {
            let button = UIButton(type: .system)
            button.setTitle(name, for: .normal)
            button.tag = index + 1
            button.addTarget(self, action: #selector(buttonPressed(_:)), for: .touchUpInside)
            stack.addArrangedSubview(button)
        }
        meter.font = .monospacedSystemFont(ofSize: 12, weight: .regular)
        meter.numberOfLines = 0
        stack.addArrangedSubview(meter)
        status.numberOfLines = 0
        status.text = "录音会保留原音与实时效果，停止后可再次调整效果并后处理。文件保存在应用 Documents 目录。"
        stack.addArrangedSubview(status)
        let arguments = ProcessInfo.processInfo.arguments
        if let index = arguments.firstIndex(of: "--mat-live-test"), index + 1 < arguments.count,
           let seconds = Int(arguments[index + 1])
        {
            diagnosticSeconds = min(1800, max(5, seconds))
            parameters.tuning = 1
            parameters.reverb = 1
            parameters.echo = 1
            parameters.denoise = 1
            if let i = arguments.firstIndex(of: "--mat-tuning"), i + 1 < arguments.count,
               let value = Float(arguments[i + 1]) { parameters.tuning = value }
            if let i = arguments.firstIndex(of: "--mat-pitch-shift"), i + 1 < arguments.count,
               let value = Float(arguments[i + 1]) { parameters.pitchShiftSemitones = value }
            pitchSlider.value = parameters.pitchShiftSemitones
            pitchLabel.text = String(format: "音高（低沉 ↔ 高尖） %+.1f 半音", parameters.pitchShiftSemitones)
            // Automatic profiling defaults to muted hardware output. Both live
            // I/O and WAV workers still run. Monitoring requires headphones.
            monitor.isOn = arguments.contains("--mat-monitor")
            UIApplication.shared.isIdleTimerDisabled = true
            DispatchQueue.main
                .asyncAfter(deadline: .now() + 1) { [weak self] in self?.permissionAndStart() }
        }
        updateParameters()
        timer = Timer
            .scheduledTimer(withTimeInterval: 0.3, repeats: true) { [weak self] _ in self?.poll() }
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(pause),
            name: UIApplication.willResignActiveNotification,
            object: nil
        )
    }

    func numberOfComponents(in _: UIPickerView) -> Int { 1 }
    func pickerView(_: UIPickerView, numberOfRowsInComponent _: Int) -> Int { keys.count }
    func pickerView(_: UIPickerView, titleForRow row: Int,
                    forComponent _: Int) -> String? { keys[row] }
    func pickerView(_: UIPickerView, didSelectRow row: Int, inComponent _: Int) {
        parameters.tonic = row
        updateParameters()
    }

    @objc func scaleChanged(_ control: UISegmentedControl) {
        parameters.scale = control.selectedSegmentIndex
        updateParameters()
    }

    @objc func amountChanged(_ slider: UISlider) {
        switch slider.tag { case 0: parameters.tuning = slider.value
        case 1: parameters.reverb = slider.value
        case 2: parameters.echo = slider.value
        default: parameters.denoise = slider.value }
        let names = ["修音（高强度电音）", "混响", "回声", "降噪（唱歌建议适中）"]
        amountLabels[slider.tag].text = "\(names[slider.tag]) \(Int(slider.value * 100))%"
        updateParameters()
    }

    @objc func pitchChanged(_ slider: UISlider) {
        let value = (slider.value * 2).rounded() / 2
        parameters.pitchShiftSemitones = value
        pitchLabel.text = String(format: "音高（低沉 ↔ 高尖） %+.1f 半音", value)
        updateParameters()
    }

    @objc func resetPitch() { pitchSlider.value = 0
        pitchChanged(pitchSlider)
    }

    func updateParameters() {
        do { try sdk.setParameters(parameters) } catch { status.text = error.localizedDescription }
    }

    @objc func buttonPressed(_ button: UIButton) {
        if busy { return }
        if button.tag == 0 { if sdk.running { stop() } else { permissionAndStart() }
            return
        }
        if sdk.running { status.text = "请先停止录音。"
            return
        }
        switch button.tag {
        case 1: player?.stop()
            let picker = UIDocumentPickerViewController(
                forOpeningContentTypes: [.wav],
                asCopy: true
            )
            picker.delegate = self
            present(picker, animated: true)
        case 2: processOffline()
        case 3: play(raw)
        case 4: play(wet)
        case 5: play(offline)
        default:
            guard let file = offline else { status.text = "先生成后处理WAV。"
                return
            }
            let share = UIActivityViewController(activityItems: [file], applicationActivities: nil)
            share.popoverPresentationController?.sourceView = button
            present(share, animated: true)
        }
    }

    func permissionAndStart() {
        AVAudioSession.sharedInstance()
            .requestRecordPermission { [weak self] granted in DispatchQueue.main.async {
                if granted { self?.begin() } else { self?.status.text = "需要在设置中允许麦克风权限。" }
            } }
    }

    func begin() {
        player?.stop()
        let prefix = "take-\(Int(Date().timeIntervalSince1970 * 1000))"
        raw = directory.appendingPathComponent(prefix + "-raw.wav")
        wet = directory.appendingPathComponent(prefix + "-live.wav")
        offline = nil
        do { try sdk.startLive(raw: raw!, processed: wet!, monitor: monitor.isOn)
            record.setTitle("停止录音", for: .normal)
            monitor.isEnabled = false
            status.text = "正在录音…"
            if diagnosticSeconds > 0 { startDiagnostic() }
        } catch {
            status.text = error.localizedDescription
            if diagnosticSeconds > 0 { writeDiagnosticFailure(error.localizedDescription) }
        }
    }

    func stop() {
        if diagnosticTimer != nil { writeDiagnosticFailure("Recording stopped before test duration")
            diagnosticTimer?.invalidate()
            diagnosticTimer = nil
        }
        sdk.stopLive()
        record.setTitle("开始录音", for: .normal)
        monitor.isEnabled = true
        status.text = "已保存原音及实时效果，可进行后处理。"
    }

    func startDiagnostic() {
        diagnosticSamples = []
        diagnosticStart = Date()
        let session = AVAudioSession.sharedInstance()
        diagnosticRoute = ["inputPorts": session.currentRoute.inputs.map(\.portType.rawValue),
                           "outputPorts": session.currentRoute.outputs.map(\.portType.rawValue),
                           "sampleRate": session.sampleRate,
                           "systemVersion": UIDevice.current.systemVersion,
                           "requestedSeconds": diagnosticSeconds,
                           "monitor": monitor.isOn,
                           "allEffects": 1,
                           "raw": raw!.lastPathComponent, "wet": wet!.lastPathComponent]
        print("MAT_PROFILE_STARTED " + String(diagnosticSeconds))
        fflush(stdout)
        diagnosticTimer = Timer
            .scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
                guard let self else { return }
                guard sdk.running, UIApplication.shared.applicationState == .active else {
                    writeDiagnosticFailure("Audio or foreground session stopped")
                    diagnosticTimer?.invalidate()
                    diagnosticTimer = nil
                    return
                }
                var row = sdk.telemetry.mapValues { $0 as Any }
                row["elapsedSeconds"] = Date().timeIntervalSince(diagnosticStart)
                diagnosticSamples.append(row)
                if diagnosticSamples
                    .count % 15 ==
                    0
                { print("MAT_PROFILE_PROGRESS " + String(diagnosticSamples.count))
                    fflush(stdout)
                }
                if diagnosticSamples.count >= diagnosticSeconds {
                    diagnosticTimer?.invalidate()
                    diagnosticTimer = nil
                    var report = diagnosticRoute
                    report["samples"] = diagnosticSamples
                    report["final"] = sdk.telemetry
                    report["elapsedSeconds"] = Date().timeIntervalSince(diagnosticStart)
                    report["tuning"] = parameters.tuning
                    report["pitchShiftSemitones"] = parameters.pitchShiftSemitones
                    stop()
                    report["diskErrorAfterClose"] = sdk.telemetry["diskError"]
                    report["completed"] = true
                    saveDiagnostic(report)
                    diagnosticSeconds = 0
                    UIApplication.shared.isIdleTimerDisabled = false
                    print("MAT_PROFILE_COMPLETE")
                    fflush(stdout)
                    runDiagnosticOffline()
                }
            }
    }

    func writeDiagnosticFailure(_ message: String) {
        diagnosticTimer?.invalidate()
        diagnosticTimer = nil
        saveDiagnostic(["completed": false, "failure": message, "samples": diagnosticSamples])
        print("MAT_PROFILE_FAILURE " + message)
        fflush(stdout)
        diagnosticSeconds = 0
        UIApplication.shared.isIdleTimerDisabled = false
    }

    func saveDiagnostic(_ report: [String: Any]) {
        do {
            let data = try JSONSerialization.data(
                withJSONObject: report,
                options: [.prettyPrinted, .sortedKeys]
            )
            try data.write(
                to: directory.appendingPathComponent("performance-ios.json"),
                options: .atomic
            )
        } catch { print("MAT_PROFILE_WRITE_ERROR " + error.localizedDescription)
            fflush(stdout)
        }
    }

    func runDiagnosticOffline() {
        let input = directory.appendingPathComponent("performance-input.wav")
        guard FileManager.default.fileExists(atPath: input.path) else { return }
        let target = directory.appendingPathComponent("performance-offline.wav")
        let snapshot = parameters.copy() as! MATAudioParameters
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let begin = Date()
            do {
                try MATAudioSDK.processWav(
                    input: input,
                    output: target,
                    parameters: snapshot,
                    offline: true,
                    appendTail: false
                )
                let seconds = Date().timeIntervalSince(begin)
                let duration = try AVAudioPlayer(contentsOf: input).duration
                let report: [String: Any] = [
                    "wallSeconds": seconds,
                    "audioSeconds": duration,
                    "rtf": seconds / duration,
                    "input": input.lastPathComponent,
                    "output": target.lastPathComponent,
                ]
                let data = try JSONSerialization.data(
                    withJSONObject: report,
                    options: [.prettyPrinted, .sortedKeys]
                )
                try data.write(
                    to: input.deletingLastPathComponent()
                        .appendingPathComponent("performance-offline.json"),
                    options: .atomic
                )
                DispatchQueue.main.async { self?.offline = target }
                print("MAT_OFFLINE_COMPLETE RTF=\(seconds / duration)")
                fflush(stdout)
            } catch { print("MAT_OFFLINE_FAILURE " + error.localizedDescription)
                fflush(stdout)
            }
        }
    }

    func processOffline() {
        guard let source = raw else { status.text = "先录音或导入WAV。"
            return
        }
        player?.stop()
        busy = true
        status.text = "正在高质量后处理…"
        let target = directory
            .appendingPathComponent("offline-\(Int(Date().timeIntervalSince1970 * 1000)).wav")
        let snapshot = parameters.copy() as! MATAudioParameters, start = Date()
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            do { try MATAudioSDK.processWav(
                input: source,
                output: target,
                parameters: snapshot,
                offline: true,
                appendTail: true
            )
            DispatchQueue.main.async { self?.offline = target
                self?.busy = false
                self?.status.text = String(
                    format: "后处理完成，耗时 %.3fs（含4秒效果尾音）\n%@",
                    Date().timeIntervalSince(start),
                    target.lastPathComponent
                )
            }
            } catch { DispatchQueue.main.async { self?.busy = false
                self?.status.text = error.localizedDescription
            } }
        }
    }

    func play(_ url: URL?) { guard let url else { status.text = "对应音频尚未生成。"
        return
    }
    do { player?.stop()
        player = try AVAudioPlayer(contentsOf: url)
        player?.play()
        status.text = "试听：\(url.lastPathComponent)"
    } catch { status.text = error.localizedDescription
    }
    }

    func documentPicker(_: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
        guard let source = urls.first else { return }
        let access = source.startAccessingSecurityScopedResource()
        defer { if access { source.stopAccessingSecurityScopedResource() } }
        do { let target = directory.appendingPathComponent("import-\(UUID().uuidString).wav")
            try FileManager.default.copyItem(at: source, to: target)
            raw = target
            wet = nil
            offline = nil
            status.text = "已导入：\(source.lastPathComponent)"
        } catch {
            status.text = error.localizedDescription
        }
    }

    func poll() {
        let t = sdk.telemetry
        func n(_ key: String) -> Double { t[key]?.doubleValue ?? 0 }
        meter.text = String(
            format: "F0 %.1f → %.1f Hz | %.2f 半音\nDSP名义延迟 %.1f ms | 回调最大 %.2f ms\nI/O缓冲 %.2f ms，输入/输出 %.2f / %.2f ms\n音频工作组可用：%.0f（系统自动管理）\n录音丢帧 %.0f，渲染/写盘错误 %.0f / %.0f",
            n("pitchHz"),
            n("targetHz"),
            n("semitones"),
            Double(sdk.latencySamples) / 48,
            n("callbackMaxMs"),
            n("ioBufferMs"),
            n("inputLatencyMs"),
            n("outputLatencyMs"),
            n("audioWorkgroupAvailable"),
            n("droppedSamples"),
            n("renderError"),
            n("diskError")
        )
        if !sdk.running, !monitor.isEnabled { record.setTitle("开始录音", for: .normal)
            monitor.isEnabled = true
            status.text = "音频中断或设备变化，已停止。可重新开始。"
        }
    }

    @objc func pause() { if sdk.running { stop() }
        player?.stop()
    }

    deinit { diagnosticTimer?.invalidate()
        timer?.invalidate()
        NotificationCenter.default.removeObserver(self)
        sdk.stopLive()
    }
}
