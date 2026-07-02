/** @type {import('tailwindcss').Config} */
export default {
  content: ["./index.html", "./src/**/*.{ts,tsx}"],
  theme: {
    extend: {
      colors: {
        app: {
          bg: "#10110f",
          panel: "#191b17",
          panel2: "#20231e",
          line: "#33382f",
          text: "#ece8df",
          muted: "#a9ad9f",
          accent: "#8fb8ff",
          accent2: "#73d79c"
        }
      },
      boxShadow: {
        soft: "0 18px 60px rgba(0, 0, 0, 0.32)"
      }
    }
  },
  plugins: []
};
