using System;
using System.Drawing;
using System.Windows.Forms;

namespace ProjectMino.Client
{
	static class Program
	{
		[STAThread]
		static void Main()
		{
			Console.WriteLine("Program.Main: starting");
			try
			{
				Application.EnableVisualStyles();
				Application.SetCompatibleTextRenderingDefault(false);
				Application.Run(new LauncherApplicationContext());
				Console.WriteLine("Program.Main: Application.Run returned normally");
			}
			catch (Exception ex)
			{
				Console.WriteLine("Program.Main: exception: " + ex);
				throw;
			}
		}
	}

	// Custom ApplicationContext so we can swap forms without exiting the message loop
	class LauncherApplicationContext : ApplicationContext
	{
		private LauncherForm? launcher;

		public LauncherApplicationContext()
		{
			launcher = new LauncherForm(OnLauncherFinished);
			// When the single window closes, exit the app.
			launcher.FormClosed += (s, e) => ExitThread();
			launcher.Show();
		}

		private void OnLauncherFinished()
		{
			// Instead of opening a new window, tell the existing launcher form to switch to game mode.
			launcher?.StartGame();
		}
	}

	class LauncherForm : Form
	{
		private readonly Ppmworks.PpmLabel currentTaskLabel;
	private readonly Ppmworks.PpmLoadingBar bar;
	private readonly System.Windows.Forms.Timer timer;
		private readonly Action finishedCallback;

		private int tickCount = 0;
		private int stage = 0;
		private readonly string[] tasks = new[] { "Loading assets...", "Initializing subsystems...", "Loading shaders...", "Finalizing..." };

		public LauncherForm(Action onFinished)
		{
			finishedCallback = onFinished;

			// Window - use normal native titlebar and chrome
			BackColor = Color.Black;
			FormBorderStyle = FormBorderStyle.Sizable;
			StartPosition = FormStartPosition.CenterScreen;
			Size = new Size(900, 600);

			// Make Escape close the launcher immediately (useful during testing)
			KeyPreview = true;
			KeyDown += (s, e) => { if (e.KeyCode == Keys.Escape) Close(); };

			// Bottom-right container for loading UI
			var container = new Panel();
			container.Size = new Size(380, 84);
			container.BackColor = Color.FromArgb(10, 10, 10); // very dark panel
			container.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
			container.Location = new Point(ClientSize.Width - container.Width - 20, ClientSize.Height - container.Height - 20);
			Controls.Add(container);

			// Label to the left of the progress bar (use ppmworks PpmLabel)
			currentTaskLabel = new Ppmworks.PpmLabel();
			currentTaskLabel.ForeColor = Color.White;
			currentTaskLabel.Font = new Font("Segoe UI", 9F, FontStyle.Regular, GraphicsUnit.Point);
			currentTaskLabel.Size = new Size(200, 60);
			currentTaskLabel.Location = new Point(8, 12);
			currentTaskLabel.Text = tasks[0];
			container.Controls.Add(currentTaskLabel);

			// Single custom PpmLoadingBar on the right
			bar = new Ppmworks.PpmLoadingBar();
			bar.Size = new Size(260, 18);
			bar.Location = new Point(container.Width - bar.Width - 12, (container.Height - bar.Height) / 2);
			bar.Value = 0;
			container.Controls.Add(bar);

			// Timer to simulate loading
			timer = new System.Windows.Forms.Timer();
			timer.Interval = 40; // ms
			timer.Tick += Timer_Tick;
			timer.Start();

			// Reposition container if the form is resized
			Resize += (s, e) =>
			{
				container.Location = new Point(ClientSize.Width - container.Width - 20, ClientSize.Height - container.Height - 20);
			};
		}

		// Switch the current window into the game UI (reuses the same Form)
		public void StartGame()
		{
			// Stop any launcher timers
			timer?.Stop();

			// Configure window chrome for a normal application window
			FormBorderStyle = FormBorderStyle.Sizable;
			Text = "ProjectMino - Game";
			BackColor = Color.DimGray;

			// Delegate to GameForm to initialize the UI inside this existing window
			var game = new GameForm();
			game.InitializeIn(this);
		}

	private void Timer_Tick(object? sender, EventArgs e)
		{
			tickCount++;

			// Single sequential loading process that moves through stages and fills the single bar
			switch (stage)
			{
				case 0:
					// fast start
					bar.Increment(4);
					if (bar.Value >= 33) { stage = 1; currentTaskLabel.Text = tasks[1]; }
					break;
				case 1:
					// mid stage
					bar.Increment(2);
					if (bar.Value >= 66) { stage = 2; currentTaskLabel.Text = tasks[2]; }
					break;
				case 2:
					// final stage
					bar.Increment(1);
					if (bar.Value >= 100) { stage = 3; currentTaskLabel.Text = tasks[3]; }
					break;
				case 3:
						// brief final wait then finish
						if (tickCount % 20 == 0)
						{
							timer.Stop();
							// Immediately transition to the game in the same window
							finishedCallback?.Invoke();
						}
					break;
			}
		}
	}
}

