using System;
using System.Collections.Generic;
using System.Timers;

namespace ProjectMino.Client
{
	// A single note/circle spawn event
	public class NoteEvent
	{
		// Time in milliseconds from map start when this note should spawn
		public int TimeMs { get; set; }
		// Spawn position (normalized 0..1 or pixel coordinates depending on game)
		public float X { get; set; }
		public float Y { get; set; }
		// Optional type for future expansion
		public int Type { get; set; }

		// Optional ARGB color for the note. If null, the game should default to white.
		public int? ColorArgb { get; set; }
	}

	// scoring state moved to ScoreStat in scorestat.cs

	// Simple CTB (catch the beat) engine: schedules NoteEvent spawns and tracks a basic score state.
	public class CtbEngine : IDisposable
	{
		private List<NoteEvent> notes = new List<NoteEvent>();
		private int nextIndex = 0;
		private DateTime startTimeUtc;
	private readonly System.Timers.Timer tickTimer;

		// Fired when the engine decides it's time to spawn a note. The UI/game should subscribe.
	public event Action<NoteEvent>? OnSpawn;

	// Scoring/statistics helper (moved into ScoreStat)
	public ScoreStat Score { get; } = new ScoreStat();

		// Tick frequency in milliseconds
		public int TickMs { get; set; } = 10;

		public CtbEngine()
		{
			tickTimer = new System.Timers.Timer(TickMs);
			tickTimer.AutoReset = true;
			tickTimer.Elapsed += Timer_Elapsed;
		}

		// Load and sort notes
		public void LoadNotes(IEnumerable<NoteEvent> noteEvents)
		{
			notes = new List<NoteEvent>(noteEvents ?? Array.Empty<NoteEvent>());
			notes.Sort((a, b) => a.TimeMs.CompareTo(b.TimeMs));
			nextIndex = 0;
		}

		// Start playback (does not play audio itself)
		public void Start(DateTime? customStartTime = null)
		{
			startTimeUtc = customStartTime ?? DateTime.UtcNow;
			nextIndex = 0;
			ResetState();
			tickTimer.Interval = TickMs;
			tickTimer.Start();
		}

		// Returns elapsed milliseconds since engine Start() was called.
		// If the engine hasn't been started, returns 0.
		public int ElapsedMs
		{
			get
			{
				if (startTimeUtc == default(DateTime)) return 0;
				try { return (int)(DateTime.UtcNow - startTimeUtc).TotalMilliseconds; }
			catch { return 0; }
			}
		}

		public void Stop()
		{
			tickTimer.Stop();
		}

		private void ResetState()
		{
			Score.Reset();
		}

	private void Timer_Elapsed(object? sender, System.Timers.ElapsedEventArgs e)
		{
			try
			{
				var elapsed = (int)(DateTime.UtcNow - startTimeUtc).TotalMilliseconds;
				while (nextIndex < notes.Count && notes[nextIndex].TimeMs <= elapsed)
				{
					var note = notes[nextIndex];
					OnSpawn?.Invoke(note);
					nextIndex++;
				}

				// Stop when done
				if (nextIndex >= notes.Count)
				{
					tickTimer.Stop();
				}
			}
			catch
			{
				// Keep engine robust; swallow exceptions from subscriber code.
			}
		}

		// Call when a note was successfully collected - delegate to ScoreStat.
		// Returns the judgment kind based on timing if caller wants it; this overload
		// keeps the original signature for compatibility and simply updates score.
		public void RegisterHit(int scoreForNote = 100)
		{
			// If caller wants timing-based judgment, they can use ScoreStat directly.
			// For backward compatibility we simply increase combo/score by one note.
			// We'll assume a neutral timing (best-effort): boost combo and add score.
			// This keeps previous behavior for code that calls RegisterHit() without timing.
			Score.RegisterHitWithTiming(0, 0, scoreForNote);
		}

		// Call when a note was missed
		public void RegisterMiss()
		{
			Score.RegisterMiss();
		}

		public void Dispose()
		{
			tickTimer?.Dispose();
		}
	}
}

