package com.moon.mobileautotune;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/** Offline major/minor key ranking. Support values are not calibrated probabilities. */
public final class KeyAnalysisResult {
  public static final int ESTIMATED = 0;
  public static final int AMBIGUOUS = 1;
  public static final int INSUFFICIENT_MELODY = 2;

  public static final class Candidate {
    public final int tonic;
    public final int scale;
    public final double support;
    public final double scaleCoverage;

    private Candidate(JSONObject json) throws JSONException {
      tonic = json.getInt("tonic");
      scale = json.getInt("scale");
      support = json.getDouble("support");
      scaleCoverage = json.getDouble("scaleCoverage");
    }
  }

  public static final class Segment {
    public final double startSeconds;
    public final double endSeconds;
    public final int tonic;
    public final int scale;
    public final double support;

    private Segment(JSONObject json) throws JSONException {
      startSeconds = json.getDouble("startSeconds");
      endSeconds = json.getDouble("endSeconds");
      tonic = json.getInt("tonic");
      scale = json.getInt("scale");
      support = json.getDouble("support");
    }
  }

  public final int status;
  public final List<Candidate> candidates;
  public final List<Segment> segments;
  public final boolean possibleModulation;
  public final double durationSeconds;
  public final double voicedSeconds;
  public final double tuningCents;
  public final double modelAgreement;
  public final double elapsedSeconds;
  public final int noteCount;
  public final int distinctPitchClasses;
  public final int threads;
  public final String swiftBackend;
  public final String rmvpeBackend;
  private final String json;

  KeyAnalysisResult(String text) throws JSONException {
    json = text;
    JSONObject value = new JSONObject(text);
    status = value.getInt("status");
    possibleModulation = value.getBoolean("possibleModulation");
    durationSeconds = value.getDouble("durationSeconds");
    voicedSeconds = value.getDouble("voicedSeconds");
    tuningCents = value.getDouble("tuningCents");
    modelAgreement = value.getDouble("modelAgreement");
    elapsedSeconds = value.getDouble("elapsedSeconds");
    noteCount = value.getInt("noteCount");
    distinctPitchClasses = value.getInt("distinctPitchClasses");
    threads = value.getInt("threads");
    swiftBackend = value.getString("swiftBackend");
    rmvpeBackend = value.getString("rmvpeBackend");
    List<Candidate> ranked = new ArrayList<>();
    JSONArray keys = value.getJSONArray("candidates");
    for (int i = 0; i < keys.length(); i++) {
      ranked.add(new Candidate(keys.getJSONObject(i)));
    }
    candidates = Collections.unmodifiableList(ranked);
    List<Segment> windows = new ArrayList<>();
    JSONArray timeline = value.getJSONArray("segments");
    for (int i = 0; i < timeline.length(); i++) {
      windows.add(new Segment(timeline.getJSONObject(i)));
    }
    segments = Collections.unmodifiableList(windows);
  }

  public String toJson() {
    return json;
  }
}
