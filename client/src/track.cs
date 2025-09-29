using System;
using System.IO;
using ManagedBass;

namespace ProjectMino.Client
{
    // Track manager using ManagedBass. Keeps a simple API: LoadFromMapFolder, Play, Pause, Stop, CurrentPositionMs.
    public class TrackManager : IDisposable
    {
        private string? songPath;
        private int streamHandle;
        private bool initialized;

        public string? SongPath => songPath;
        public string? LastError { get; private set; }
        public bool IsLoaded => !string.IsNullOrEmpty(songPath) && streamHandle != 0;
        public bool IsPlaying => streamHandle != 0 && Bass.ChannelIsActive(streamHandle) == PlaybackState.Playing;

        private string DebugLogPath => Path.Combine(Path.GetTempPath(), "ProjectMino_playback_debug.txt");

        public TrackManager()
        {
            try
            {
                if (!initialized)
                {
                    // Attempt init and log result
                    if (Bass.Init(-1, 44100, DeviceInitFlags.Default))
                    {
                        initialized = true;
                        AppendDebug("ManagedBass initialized successfully");
                    }
                    else
                    {
                        AppendDebug($"ManagedBass init returned false: {Bass.LastError}");
                    }
                }
            }
            catch (Exception ex)
            {
                // Write rich diagnostics to help identify architecture mismatch
                try
                {
                    AppendDebug($"ManagedBass.Init exception: {ex.GetType().FullName}: {ex.Message}");
                    AppendDebug($"Is64BitProcess={Environment.Is64BitProcess}, Is64BitOS={Environment.Is64BitOperatingSystem}");
                }
                catch { }
                throw;
            }
        }

        private void AppendDebug(string line)
        {
            try
            {
                var dir = Path.GetDirectoryName(DebugLogPath);
                if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir)) Directory.CreateDirectory(dir);
                using (var w = new StreamWriter(DebugLogPath, true, System.Text.Encoding.UTF8))
                {
                    w.WriteLine($"[{DateTime.Now:O}] {line}");
                    w.Flush();
                }
            }
            catch { /* best-effort logging only */ }
        }

        // Load the song file from map folder. Returns true if a song was found.
        public bool LoadFromMapFolder(string mapFolderPath)
        {
            if (string.IsNullOrEmpty(mapFolderPath)) return false;
            var metaPath = Path.Combine(mapFolderPath, "map.json");
            var meta = CtbLoader.LoadMetadata(metaPath);
            if (meta == null || string.IsNullOrWhiteSpace(meta.SongFile)) return false;

            AppendDebug($"LoadFromMapFolder: mapFolder={mapFolderPath}, SongFile={meta.SongFile}");

            var candidate = Path.Combine(mapFolderPath, meta.SongFile);
            if (File.Exists(candidate)) { songPath = Path.GetFullPath(candidate); AppendDebug($"Found candidate: {songPath}"); return true; }
            if (File.Exists(meta.SongFile)) { songPath = Path.GetFullPath(meta.SongFile); AppendDebug($"Found absolute path: {songPath}"); return true; }
            var alt = Path.Combine(mapFolderPath, "assets", meta.SongFile);
            if (File.Exists(alt)) { songPath = Path.GetFullPath(alt); AppendDebug($"Found in assets/: {songPath}"); return true; }

            try
            {
                var nameOnly = Path.GetFileName(meta.SongFile);
                if (!string.IsNullOrEmpty(nameOnly))
                {
                    var found = Directory.GetFiles(mapFolderPath, "*", SearchOption.AllDirectories);
                    foreach (var f in found)
                    {
                        if (string.Equals(Path.GetFileName(f), nameOnly, StringComparison.OrdinalIgnoreCase))
                        {
                            songPath = Path.GetFullPath(f);
                            AppendDebug($"Found by recursive search: {songPath}");
                            return true;
                        }
                    }
                }
            }
            catch (Exception ex) { AppendDebug("Recursive search failed: " + ex.Message); }

            AppendDebug("LoadFromMapFolder: song not found");
            return false;
        }

        // Play the loaded song
        public bool Play()
        {
            // If we already have an active playing stream, treat repeated Play() as a no-op
            try
            {
                if (IsPlaying)
                {
                    AppendDebug("Play() called but already playing - no action taken");
                    return true;
                }
            }
            catch { }

            if (string.IsNullOrEmpty(songPath)) { LastError = "No song path specified"; AppendDebug("Play() failed: " + LastError); return false; }
            if (!File.Exists(songPath)) { LastError = $"Audio file not found: {songPath}"; AppendDebug("Play() failed: " + LastError); return false; }

            LastError = null;
            AppendDebug($"Play() starting with file: {songPath}");

            try
            {
                // Stop any existing stream
                Stop();

                // Create the stream and record the handle and any immediate error
                streamHandle = Bass.CreateStream(songPath, 0, 0, BassFlags.Default);
                AppendDebug($"CreateStream returned handle={streamHandle}, LastError={Bass.LastError}");
                if (streamHandle == 0)
                {
                    LastError = $"CreateStream failed: {Bass.LastError}";
                    AppendDebug(LastError);
                    return false;
                }

                var info = Bass.ChannelGetInfo(streamHandle);
                AppendDebug($"Audio format: {info.Frequency}Hz, {info.Channels} channels, Flags={info.Flags}");

                // Attempt to start playing and capture any immediate errors
                var playOk = Bass.ChannelPlay(streamHandle);
                AppendDebug($"ChannelPlay returned {playOk}, LastError={Bass.LastError}");
                if (!playOk)
                {
                    LastError = $"ChannelPlay failed: {Bass.LastError}";
                    AppendDebug(LastError);
                    return false;
                }

                // Poll the channel state briefly to detect whether it becomes active
                try
                {
                    for (int i = 0; i < 30; i++)
                    {
                        var state = Bass.ChannelIsActive(streamHandle);
                        var pos = Bass.ChannelGetPosition(streamHandle);
                        var seconds = Bass.ChannelBytes2Seconds(streamHandle, pos);
                        AppendDebug($"Poll[{i}]: State={state}, PositionSeconds={seconds:F3}, LastError={Bass.LastError}");
                        if (state == PlaybackState.Playing) break;
                        System.Threading.Thread.Sleep(100);
                    }
                }
                catch (Exception ex)
                {
                    AppendDebug($"Polling exception: {ex.GetType().FullName}: {ex.Message}");
                }

                var finalState = Bass.ChannelIsActive(streamHandle);
                AppendDebug($"Final channel state after start attempt: {finalState}");
                if (finalState != PlaybackState.Playing)
                {
                    LastError = $"Channel not playing after start (State={finalState}, LastError={Bass.LastError})";
                    AppendDebug(LastError);
                    // keep the stream open for inspection but report failure
                    return false;
                }

                AppendDebug("Audio playback started and channel is playing");
                return true;
            }
            catch (Exception ex)
            {
                LastError = ex.Message;
                AppendDebug($"Play() exception: {ex.Message}");
                return false;
            }
        }

        public void Pause()
        {
            if (streamHandle != 0)
            {
                Bass.ChannelPause(streamHandle);
                AppendDebug("Playback paused");
            }
        }

        public void Stop()
        {
            if (streamHandle != 0)
            {
                AppendDebug("Stop() called");
                Bass.ChannelStop(streamHandle);
                Bass.StreamFree(streamHandle);
                streamHandle = 0;
                AppendDebug("Stopped and freed stream");
            }
        }

        public double CurrentPositionMs
        {
            get
            {
                if (streamHandle != 0)
                {
                    var pos = Bass.ChannelGetPosition(streamHandle);
                    var seconds = Bass.ChannelBytes2Seconds(streamHandle, pos);
                    return seconds * 1000.0;
                }
                return 0.0;
            }
        }

        public void Dispose()
        {
            try
            {
                Stop();
                if (initialized)
                {
                    Bass.Free();
                    initialized = false;
                    AppendDebug("ManagedBass freed");
                }
            }
            catch { }
        }
    }
}
