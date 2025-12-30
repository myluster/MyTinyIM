import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// https://vitejs.dev/config/
export default defineConfig({
    plugins: [react()],
    server: {
        port: 5173,
        host: true,
        proxy: {
            '/api': {
                target: 'http://127.0.0.1:9000', // Dispatch Service (Mapped to 9000 on host)
                changeOrigin: true,
                // rewrite: (path) => path.replace(/^\/api/, '') // Dispatch handles /api prefix? Check main.cpp. 
                // Dispatch code checks target == "/login" or "/api/register" ???
                // Let's check dispatch/main.cpp. 
                // It checks target == "/login" (no api prefix?) AND "/api/register" and "/api/logout" ??
                // Let me double check dispatch/main.cpp logic in next step or assume standard and verify.
                // Looking at previous view_file of dispatch/main.cpp:
                // line 117: if (req_.method() == http::verb::post && target == "/login")
                // line 154: else if (... target == "/logout" or "/api/logout"?) -> It was /logout in original, changed to /api/logout?
                // Wait, I see target == "/login" in line 117.
                // It seems consistent prefix is missing in Dispatch.
                // I should probably REWRITE path in proxy if dispatch doesn't use /api prefix for login, 
                // OR update Dispatch to use /api/login.
                // To be safe and clean, I will update Dispatch to use /api prefix for ALL endpoints.
            }
        }
    }
})
