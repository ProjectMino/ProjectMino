using System;
using System.Drawing;
using System.Windows.Forms;
using Ppmworks;

namespace ProjectMino.Client
{
    // Simple instantaneous-moving 'bowl' bar that snaps left/right on key press.
    public class Bowl
    {
        private int x; // center X in pixels
        private int barWidth = 120;
        private int barHeight = 10;
        private int distanceAboveBottom = 120;
    // movement speed in pixels per second when holding a key
    private int moveSpeed = 2200;
        private bool leftPressed;
        private bool rightPressed;

        public Bowl()
        {
            x = 0;
        }

        // set flags for continuous movement
        public void OnKeyDown(Keys k)
        {
            if (k == Keys.Left || k == Keys.A) leftPressed = true;
            if (k == Keys.Right || k == Keys.D) rightPressed = true;
        }

        public void OnKeyUp(Keys k)
        {
            if (k == Keys.Left || k == Keys.A) leftPressed = false;
            if (k == Keys.Right || k == Keys.D) rightPressed = false;
        }

        // Move continuously while keys are held; dt in seconds
        public void Update(float dt, int windowWidth, int windowHeight)
        {
            if (windowWidth <= 0) return;
            if (x == 0) x = windowWidth / 2;

            int dir = 0;
            if (leftPressed) dir -= 1;
            if (rightPressed) dir += 1;

            if (dir != 0)
            {
                x += (int)Math.Round(dir * moveSpeed * dt);
            }

            int half = barWidth / 2;
            if (x < half) x = half;
            if (x > windowWidth - half) x = windowWidth - half;
        }

        public void Draw(PpmRenderer rnd)
        {
            if (rnd == null) return;
            int y = Math.Max(40, rnd.Height - distanceAboveBottom);
            var lineRect = new Rectangle(0, y, rnd.Width, 2);
            rnd.DrawFilledRect(lineRect, Color.FromArgb(200, 200, 200));
            var barRect = new Rectangle(x - barWidth / 2, y - barHeight / 2, barWidth, barHeight);
            // Draw bar as a solid white rectangle with no outline
            rnd.DrawFilledRect(barRect, Color.White);
        }

        // Provide the current bar rectangle for collision detection / positioning
        public Rectangle GetBarRect(int windowWidth, int windowHeight)
        {
            if (windowWidth <= 0) windowWidth = 1;
            if (x == 0) x = windowWidth / 2;
            int y = Math.Max(40, windowHeight - distanceAboveBottom);
            return new Rectangle(x - barWidth / 2, y - barHeight / 2, barWidth, barHeight);
        }
    }
}
