import {
  Alert, Badge, Container, Group, List, SegmentedControl, Stack, Text, Title,
} from '@mantine/core'
import { useCallback, useMemo, useState } from 'react'

import ScoreView from '@/components/ScoreView'
import { examples } from '@/core/examples'
import { validate } from '@/core/validation/validate'
import type { RenderResult } from '@/render/renderScore'

const Index = () => {
  const [selected, setSelected] = useState(examples[0]!.name)
  const [renderWarnings, setRenderWarnings] = useState<string[]>([])

  const music = useMemo(() => examples.find(e => e.name === selected)!.build(), [selected])
  const problems = useMemo(() => validate(music), [music])
  const onRendered = useCallback((result: RenderResult) => setRenderWarnings(result.warnings), [])

  return (
    <Container size="lg" py="md">
      <Stack gap="md">
        <Group justify="space-between">
          <Title order={3}>Notation prototype</Title>
          <Badge color={problems.length === 0 ? 'teal' : 'red'} variant="light">
            {problems.length === 0 ? 'valid' : `${problems.length} problems`}
          </Badge>
        </Group>

        <SegmentedControl
          data={examples.map(e => e.name)}
          value={selected}
          onChange={setSelected}
        />

        {problems.length > 0 && (
          <Alert color="red" title="Validation problems">
            <List size="sm">
              {problems.map((p, i) => (
                <List.Item key={i}>
                  <Text span fw={600}>{p.code}</Text>
                  {' '}{p.message}{' '}
                  <Text span c="dimmed">({p.path})</Text>
                </List.Item>
              ))}
            </List>
          </Alert>
        )}

        {renderWarnings.length > 0 && (
          <Alert color="yellow" title="Rendering warnings">
            <List size="sm">
              {renderWarnings.map((w, i) => <List.Item key={i}>{w}</List.Item>)}
            </List>
          </Alert>
        )}

        <ScoreView music={music} onRendered={onRendered} />
      </Stack>
    </Container>
  )
}

export default Index
