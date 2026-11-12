import { createRoot } from 'react-dom/client'
import App from './App'
import './styles.css'

// 不用 StrictMode：effect 双跑会开两条 WS，底座的 1:1 会话会直接 409。
createRoot(document.getElementById('root')!).render(<App />)
