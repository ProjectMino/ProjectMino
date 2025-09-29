using System;
using System.Drawing;
using System.Collections.Generic;
using System.IO;
using System.Windows.Forms;
using Ppmworks;

namespace ProjectMino.Client
{
	public class GameForm : Form
	{
		// Initialize game UI into an existing host form instead of using a separate GameForm window.
		public void InitializeIn(Form host)
		{
            // helper moved here to be shared

            // Sentinel: indicate InitializeIn was entered (helps diagnose whether TrackManager is reached)
            try { File.AppendAllText(Path.Combine(Path.GetTempPath(), "ProjectMino_sentinel.txt"), $"InitializeIn entered: {DateTime.Now:O}\r\n"); } catch { }
            // Copy relevant properties and move UI into the host
            host.Text = "ProjectMino - Game";
			host.StartPosition = FormStartPosition.CenterScreen;
			host.Size = new Size(1024, 768);
			host.BackColor = Color.DimGray;

			// Remove existing controls and hook up game UI
			host.Controls.Clear();

			canvas = new PictureBox();
			canvas.Dock = DockStyle.Fill;
			canvas.BackColor = Color.Black;
			canvas.SizeMode = PictureBoxSizeMode.Normal;
			canvas.TabStop = true;
			// make canvas focusable so it can receive keyboard focus on click
			canvas.TabStop = true;
			// ensure arrow keys are treated as input when the canvas has focus
			canvas.PreviewKeyDown += (s, e) => {
				if (e.KeyCode == Keys.Left || e.KeyCode == Keys.Right || e.KeyCode == Keys.Up || e.KeyCode == Keys.Down || e.KeyCode == Keys.A || e.KeyCode == Keys.D)
					e.IsInputKey = true;
			};
			canvas.MouseDown += (s, e) => { canvas.Focus(); };
			canvas.KeyDown += (s, e) => { bowl?.OnKeyDown(e.KeyCode); };
			canvas.KeyUp += (s, e) => { bowl?.OnKeyUp(e.KeyCode); };
			host.Controls.Add(canvas);
			// playback status label
			playbackStatusLabel = new Label();
			playbackStatusLabel.AutoSize = true;
			playbackStatusLabel.ForeColor = Color.White;
			playbackStatusLabel.BackColor = Color.Transparent;
			playbackStatusLabel.Location = new Point(8, 8);
			playbackStatusLabel.Anchor = AnchorStyles.Top | AnchorStyles.Left;
			host.Controls.Add(playbackStatusLabel);

			// Renderer sized to client area
			renderer = new PpmRenderer(host.ClientSize.Width, host.ClientSize.Height);

			// Removed PpmButton and its event wiring

			// Resize handler to resize renderer
			host.Resize += (s, e) =>
			{
				renderer.Resize(host.ClientSize.Width, host.ClientSize.Height);
			};

			// Game tick (use ~60 FPS for smoother input)
			tickTimer = new System.Windows.Forms.Timer();
			tickTimer.Interval = 16; // ~60 FPS
			tickTimer.Tick += (s, e) => { UpdateFrame(); };
			tickTimer.Start();

			// Close on escape + forward keys to bowl
			host.KeyPreview = true;
			host.KeyDown += (s, e) => { if (e.KeyCode == Keys.Escape) host.Close(); else { bowl?.OnKeyDown(e.KeyCode); } };
			host.KeyUp += (s, e) => { bowl?.OnKeyUp(e.KeyCode); };

			// create bowl and initialize timing
			bowl = new Bowl();
			// use Stopwatch for higher-resolution timing
			stopwatch = new System.Diagnostics.Stopwatch();
			stopwatch.Start();

			// Setup CTB engine and load map
			try
			{
				engine = new CtbEngine();
				engine.OnSpawn += (ev) => {
					// spawn note; interpret X in 0..100 as percent
					float fx = ev.X;
					if (fx >= 0 && fx <= 100) fx = fx / 100f * renderer.Width;
					Color c = Color.White;
					if (ev.ColorArgb.HasValue) c = Color.FromArgb(ev.ColorArgb.Value);
					var note = new FallingNote { X = fx, Y = -32, Color = c };
					// Marshal additions to the UI thread to avoid concurrent modification while UpdateFrame iterates the list
					try
					{
						if (canvas != null && canvas.IsHandleCreated)
						{
							canvas.BeginInvoke(new Action(() => activeNotes.Add(note)));
						}
						else
						{
							// fallback (should be rare during startup)
							activeNotes.Add(note);
						}
					}
					catch
					{
						// If BeginInvoke fails for any reason, try to add directly
						try { activeNotes.Add(note); } catch { }
					}
				};


				var mapFolder = ResolveMap("0000000001");
				try { File.AppendAllText(Path.Combine(Path.GetTempPath(), "game_trace.txt"), $"mapFolder resolved: {mapFolder}\r\n"); } catch { }
				// Attempt to load the map background (if provided in map.json)
				try
				{
					background?.Dispose();
					background = new Background();
					background.LoadFromMapFolder(mapFolder);
				}
				catch { }
				try
				{
					CtbLoader.LoadMapIntoEngine(mapFolder, engine);
					try { File.AppendAllText(Path.Combine(Path.GetTempPath(), "game_trace.txt"), $"Loaded map into engine\r\n"); } catch { }
				}
				catch (Exception ex) { try { File.AppendAllText(Path.Combine(Path.GetTempPath(), "game_trace.txt"), $"LoadMapIntoEngine exception: {ex}\r\n"); } catch { } }
				// Try to load and play the track for this map
				// Note: Start engine after track is ready to ensure sync
				try
				{
					try { File.AppendAllText(Path.Combine(Path.GetTempPath(), "game_trace.txt"), "About to create TrackManager\r\n"); } catch { }
						if (trackManager == null)
						{
							trackManager = new TrackManager();
							try { File.AppendAllText(Path.Combine(Path.GetTempPath(), "game_trace.txt"), "TrackManager constructed\r\n"); } catch { }
						}
					if (trackManager.LoadFromMapFolder(mapFolder))
					{
						try { File.AppendAllText(Path.Combine(Path.GetTempPath(), "game_trace.txt"), "LoadFromMapFolder returned true\r\n"); } catch { }
						var ok = trackManager.Play();
						if (ok)
						{
							host.Text = $"ProjectMino - Game (Playing: {Path.GetFileName(trackManager.SongPath)})";
							// Start engine with current time to sync with audio
							engine.Start(DateTime.UtcNow);
						}
						else
						{
							MessageBox.Show(host, $"Failed to play track: {trackManager.LastError}", "Playback Error", MessageBoxButtons.OK, MessageBoxIcon.Warning);
							try { File.AppendAllText(Path.Combine(Path.GetTempPath(), "game_trace.txt"), $"Play() failed: {trackManager.LastError}\r\n"); } catch { }
						}
					}
					else { try { File.AppendAllText(Path.Combine(Path.GetTempPath(), "game_trace.txt"), "LoadFromMapFolder returned false\r\n"); } catch { } }
				}
				catch (Exception ex) { try { File.AppendAllText(Path.Combine(Path.GetTempPath(), "game_trace.txt"), $"TrackManager try/catch exception: {ex}\r\n"); } catch { } }
				// Spawn one immediate test note so player sees something without waiting
				activeNotes.Add(new FallingNote { X = renderer.Width / 2, Y = -32, Color = Color.White });
			}
			catch { }
		}
		private PictureBox canvas;
		private PpmRenderer renderer;
		// Background image for the current map (may be null)
		private Background? background;
	// runtime engine and active notes
	private CtbEngine? engine;
		private List<FallingNote> activeNotes = new List<FallingNote>();

		private class FallingNote
		{
			public float X;
			public float Y;
			public int Radius = 36;
			public float Speed = 160f; // pixels per second
			public Color Color = Color.White;
		}

		// Simple particle for splash effect
		private class Particle
		{
			public float X;
			public float Y;
			public float VX;
			public float VY;
			public float Size;
			public float Life;
			public float MaxLife;
			public Color Color;
		}

		private List<Particle> particles = new List<Particle>();
		private Random rand = new Random();

		// Combo display state
		private int lastCombo = 0;
		private float comboScale = 1f;
		private float comboScaleVelocity = 0f;
		private System.Drawing.Font comboFont = new System.Drawing.Font("Arial", 18, FontStyle.Bold);
		private Color comboColor = Color.Gold;

		private void SpawnSplash(int x, int y, int count, Color baseColor)
		{
			for (int i = 0; i < count; i++)
			{
				var angle = (float)(rand.NextDouble() * Math.PI * 2.0);
				var speed = (float)(60 + rand.NextDouble() * 180);
				var p = new Particle
				{
					X = x + (float)(rand.NextDouble() * 8 - 4),
					Y = y + (float)(rand.NextDouble() * 8 - 4),
					VX = (float)(Math.Cos(angle) * speed),
					VY = (float)(Math.Sin(angle) * speed) - 80f,
					Size = (float)(4 + rand.NextDouble() * 8),
					Life = 0f,
					MaxLife = (float)(0.4 + rand.NextDouble() * 0.6),
					// Use the base color with slight variation in brightness
					Color = VariateColor(baseColor)
				};
				particles.Add(p);
			}
		}

		private Color VariateColor(Color baseColor)
		{
			// apply a small random brightness variation (0.75 - 1.15)
			double mult = 0.75 + rand.NextDouble() * 0.4;
			int r = (int)Math.Round(Math.Min(255, baseColor.R * mult));
			int g = (int)Math.Round(Math.Min(255, baseColor.G * mult));
			int b = (int)Math.Round(Math.Min(255, baseColor.B * mult));
			return Color.FromArgb(baseColor.A, r, g, b);
		}

		// Bowl instance and timing for smooth updates
		private Bowl bowl;
		private System.Diagnostics.Stopwatch stopwatch;
	private System.Windows.Forms.Timer tickTimer;

        // Track manager for music playback
	private TrackManager? trackManager;

		// Playback status UI
		private Label playbackStatusLabel;

		public GameForm()
		{
			// Support constructing as a standalone form if desired; otherwise use InitializeIn(host) to embed
			Text = "ProjectMino - Game";
			StartPosition = FormStartPosition.CenterScreen;
			Size = new Size(1024, 768);
			BackColor = Color.DimGray;

			canvas = new PictureBox();
			canvas.Dock = DockStyle.Fill;
			canvas.BackColor = Color.Black;
			canvas.SizeMode = PictureBoxSizeMode.Normal;
			Controls.Add(canvas);
			// playback status label
			playbackStatusLabel = new Label();
			playbackStatusLabel.AutoSize = true;
			playbackStatusLabel.ForeColor = Color.White;
			playbackStatusLabel.BackColor = Color.Transparent;
			playbackStatusLabel.Location = new Point(8, 8);
			playbackStatusLabel.Anchor = AnchorStyles.Top | AnchorStyles.Left;
			Controls.Add(playbackStatusLabel);
			// ensure arrow keys are treated as input when the canvas has focus
			canvas.TabStop = true;
			canvas.PreviewKeyDown += (s, e) => {
				if (e.KeyCode == Keys.Left || e.KeyCode == Keys.Right || e.KeyCode == Keys.Up || e.KeyCode == Keys.Down || e.KeyCode == Keys.A || e.KeyCode == Keys.D)
					e.IsInputKey = true;
			};
			canvas.MouseDown += (s, e) => { canvas.Focus(); };
			canvas.KeyDown += (s, e) => { bowl?.OnKeyDown(e.KeyCode); };
			canvas.KeyUp += (s, e) => { bowl?.OnKeyUp(e.KeyCode); };

			// Renderer sized to client area
			renderer = new PpmRenderer(ClientSize.Width, ClientSize.Height);

			// Removed PpmButton and its event wiring

			// Resize handler to resize renderer
			Resize += (s, e) =>
			{
				renderer.Resize(ClientSize.Width, ClientSize.Height);
			};

			// Game tick
			// Game tick (use ~60 FPS for smoother input)
			tickTimer = new System.Windows.Forms.Timer();
			tickTimer.Interval = 16; // ~60 FPS
			tickTimer.Tick += (s, e) => { UpdateFrame(); };
			tickTimer.Start();

			// Close on escape + forward keys to bowl
			KeyPreview = true;
			KeyDown += (s, e) => { if (e.KeyCode == Keys.Escape) Close(); else { bowl?.OnKeyDown(e.KeyCode); } };
			KeyUp += (s, e) => { bowl?.OnKeyUp(e.KeyCode); };

			// create bowl and initialize timing
			bowl = new Bowl();
			stopwatch = new System.Diagnostics.Stopwatch();
			stopwatch.Start();

			// Setup CTB engine and load map
			try
			{
				engine = new CtbEngine();
				engine.OnSpawn += (ev) => {
					float fx = ev.X;
					if (fx >= 0 && fx <= 100) fx = fx / 100f * renderer.Width;
					Color c = Color.White;
					if (ev.ColorArgb.HasValue) c = Color.FromArgb(ev.ColorArgb.Value);
					var note = new FallingNote { X = fx, Y = -32, Color = c };
					try
					{
						if (canvas != null && canvas.IsHandleCreated)
						{
							canvas.BeginInvoke(new Action(() => activeNotes.Add(note)));
						}
						else
						{
							activeNotes.Add(note);
						}
					}
					catch
					{
						try { activeNotes.Add(note); } catch { }
					}
				};
				var mapFolder2 = ResolveMap("0000000001");
				// Load background image for this map (if present)
				try
				{
					background?.Dispose();
					background = new Background();
					background.LoadFromMapFolder(mapFolder2);
				}
				catch { }
				CtbLoader.LoadMapIntoEngine(mapFolder2, engine);
				// Try to load and play the track for this map, start engine after track is ready to ensure sync
				try
				{
					if (trackManager == null)
					{
						trackManager = new TrackManager();
						try { File.AppendAllText(Path.Combine(Path.GetTempPath(), "TrackManager_created.txt"), $"created at ctor: {DateTime.Now:O}\r\n"); } catch { }
					}
					if (trackManager.LoadFromMapFolder(mapFolder2))
					{
						var ok2 = trackManager.Play();
						if (ok2)
						{
							this.Text = $"ProjectMino - Game (Playing: {Path.GetFileName(trackManager.SongPath)})";
							// Start engine with current time to sync with audio
							engine.Start(DateTime.UtcNow);
						}
						else
						{
							MessageBox.Show(this, $"Failed to play track: {trackManager.LastError}", "Playback Error", MessageBoxButtons.OK, MessageBoxIcon.Warning);
						}
					}
				}
				catch { }
				// Spawn one immediate test note so player sees something without waiting
				activeNotes.Add(new FallingNote { X = renderer.Width / 2, Y = -32, Color = Color.White });
			}
			catch { }

			// ensure canvas gets focus when the form is shown
			this.Shown += (s, e) => { canvas.Focus(); };
		}

		private void UpdateFrame()
		{
			// Simple demo render
			if (renderer == null) return; // guard against null renderer
			// Draw map background stretched to the renderer size if available
			if (background != null && background.HasImage)
			{
				background.Draw(renderer);
			}
			else
			{
				renderer.Clear(Color.FromArgb(24, 24, 24));
			}

			// Only drawing the bowl and its line

			// Update bowl with delta time and draw it
			float dt = 1f / 60f;
			try {
				if (stopwatch != null)
				{
					dt = (float)stopwatch.Elapsed.TotalSeconds;
					stopwatch.Restart();
				}
			}
			catch { dt = 1f/60f; }

			bowl?.Update(dt, renderer.Width, renderer.Height);
			bowl?.Draw(renderer);

			// Combo animation: detect changes and pop
			int currentCombo = engine?.State?.Combo ?? 0;
			if (currentCombo != lastCombo)
			{
				// trigger pop when combo increases
				if (currentCombo > lastCombo)
				{
					comboScale = 1.4f;
					comboScaleVelocity = -3f; // shrink back
				}
				else if (currentCombo < lastCombo)
				{
					// small shake on miss
					comboScale = 0.9f;
					comboScaleVelocity = 2f;
				}
				lastCombo = currentCombo;
			}

			// Update combo scale physics
			comboScale += comboScaleVelocity * dt;
			// damp
			comboScaleVelocity *= 0.9f;
			if (comboScale < 0.5f) comboScale = 0.5f;
			if (comboScale > 2.0f) comboScale = 2.0f;

			// Update falling notes
			for (int i = activeNotes.Count - 1; i >= 0; i--)
			{
				var n = activeNotes[i];
				n.Y += n.Speed * dt; // move down
				// If note overlaps bowl bar, register hit
				if (bowl == null || renderer == null) continue;
				var bar = bowl.GetBarRect(renderer.Width, renderer.Height);
				var noteRect = new Rectangle((int)(n.X - n.Radius), (int)(n.Y - n.Radius), n.Radius * 2, n.Radius * 2);
				if (noteRect.IntersectsWith(bar))
				{
					engine?.RegisterHit();
					// set combo color from this note
					comboColor = n.Color;
					// spawn splash particles using the note color
					SpawnSplash((int)n.X, (int)n.Y, 12, n.Color);
					activeNotes.RemoveAt(i);
					continue;
				}
				// If passed bottom, it's a miss
				if (n.Y - n.Radius > renderer.Height)
				{
					engine?.RegisterMiss();
					activeNotes.RemoveAt(i);
					continue;
				}
			}

			// Draw active notes
			if (renderer != null)
			{
				// Iterate over a snapshot to avoid enumeration exceptions if notes are added/removed concurrently
				var snapshot = activeNotes.ToArray();
				foreach (var n in snapshot)
				{
					var r = new Rectangle((int)(n.X - n.Radius), (int)(n.Y - n.Radius), n.Radius * 2, n.Radius * 2);
					// Draw notes as purely filled white circles with no outline
					renderer.DrawFilledEllipse(r, Color.White);
				}
			}

			// Update particles
			for (int i = particles.Count - 1; i >= 0; i--)
			{
				var p = particles[i];
				p.Life += dt;
				if (p.Life >= p.MaxLife) { particles.RemoveAt(i); continue; }
				p.VY += 300f * dt; // gravity
				p.X += p.VX * dt;
				p.Y += p.VY * dt;
				// draw particle (fade by life)
				if (renderer != null)
				{
					float t = 1f - (p.Life / p.MaxLife);
					var col = Color.FromArgb((int)(t * 255), p.Color);
					renderer.DrawFilledEllipse(new Rectangle((int)(p.X - p.Size/2), (int)(p.Y - p.Size/2), (int)p.Size, (int)p.Size), col);
				}
			}

			// no extra UI

			// Draw combo above bowl
			if (engine != null)
			{
				if (bowl != null && renderer != null && engine.State != null)
				{
					var bar = bowl.GetBarRect(renderer.Width, renderer.Height);
					var cx = bar.X + bar.Width / 2f;
					var cy = bar.Y - 56f; // higher above the bar
					if (engine.State.Combo > 0)
					{
						string text = $"{engine.State.Combo}x";
					// scale font by comboScale
					float baseSize = 18f * comboScale;
					using (var f = new Font(comboFont.FontFamily, baseSize, comboFont.Style))
					{
						renderer.DrawTextCentered(text, f, comboColor, new PointF(cx, cy));
					}
				}
			}
		}

			// Update playback status label
			try
			{
				if (trackManager != null && playbackStatusLabel != null)
				{
					var name = trackManager.SongPath != null ? Path.GetFileName(trackManager.SongPath) : "(none)";
					var playing = trackManager.IsPlaying ? "Playing" : "Stopped";
					var pos = trackManager.CurrentPositionMs;
					playbackStatusLabel.Text = $"Track: {name} | {playing} | {pos:0} ms";
				}
			}
			catch { }

		// Present to PictureBox (fast path)
		if (renderer != null && canvas != null) renderer.PresentTo(canvas);
		}

		// Resolve map folder by walking up from the base directory to find the project's maps folder
		private string ResolveMap(string mapId)
		{
			var dir = AppDomain.CurrentDomain.BaseDirectory;
			for (int i = 0; i < 8; i++)
			{
				var candidate = Path.GetFullPath(Path.Combine(dir, "maps", mapId));
				if (Directory.Exists(candidate)) return candidate;
				// go up one directory
				var parent = Path.GetFullPath(Path.Combine(dir, ".."));
				if (parent == dir) break;
				dir = parent;
			}
			// fallback to original relative path
			return Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "..", "maps", mapId));
		}

		protected override void OnFormClosed(FormClosedEventArgs e)
		{
			base.OnFormClosed(e);
			tickTimer?.Stop();
			renderer?.Dispose();
			background?.Dispose();
			try { trackManager?.Dispose(); } catch { }
		}
	}
}

