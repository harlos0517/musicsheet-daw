import { createTheme, MantineProvider } from '@mantine/core'

import Index from '@/pages/Index'

import '@mantine/core/styles.css'

import './assets/styles/main.sass'


const theme = createTheme({})

export const App = () =>
  <MantineProvider theme={theme} defaultColorScheme="dark">
    <Index />
  </MantineProvider>

export default App
