import React from 'react';
import {AbsoluteFill} from 'remotion';
import {Card, Eyebrow, Head, Lower, Sub} from '../components/Editorial';
import {At, Crop} from '../components/Motion';
import {PhotoHero} from '../components/PhotoHero';
import {AnswerScreen, AskMenuScreen, ReaderMenuScreen, ReaderScreen, WaitScreen} from '../screens/Screens';
import {PASSAGE} from './passage';

const QUESTION =
  'You have just read that what stands in the way becomes the way. Say it back without the metaphor: what is Aurelius claiming can never be impeded, and why does that follow?';

// The agent is part of the reader, not a chat window bolted on. It is a row in
// the reader's own menu, and the passage is chosen for you: launchAskBook()
// calls inkagent::choosePassageWindow() around where you are on the page
// (EpubReaderActivity.cpp:1022). There is nothing to select and nothing to mark.
export const Native: React.FC = () => (
  <AbsoluteFill>
    <At from={0} to={70}>
      <Card>
        <Eyebrow>Native AI</Eyebrow>
        <Head>The agent lives in the firmware.</Head>
        <Sub>
          Not an app you switch to. It is a row in the reader's own menu, and the passage
          you are on is already the subject — the firmware picks the window around your
          page itself.
        </Sub>
      </Card>
    </At>

    <At from={70} to={120}>
      <PhotoHero zoom={1.1} dark>
        <ReaderMenuScreen />
      </PhotoHero>
      <Lower label="The reader's own menu" title="Ask the book, in the list." />
    </At>

    <At from={120} to={175}>
      <PhotoHero zoom={1.1} zoomTo={1.16} over={55} dark>
        <AskMenuScreen selected={0} />
      </PhotoHero>
      <Lower
        label="Ask the book"
        title="Question · Explain · Story so far · Who is this · Translate"
        body="Question me on this is first. Retrieval is what makes reading stick, so it is the default, not a mode."
      />
    </At>

    <At from={175} to={200}>
      <PhotoHero zoom={1.16} dark>
        <WaitScreen />
      </PhotoHero>
      <Lower
        label="Your relay"
        title="Your key. Your model."
        body="An API key, an OpenAI-compatible endpoint, Ollama, or your own agent. The device holds a pairing token and nothing else."
      />
    </At>

    <At from={200} to={250}>
      <PhotoHero zoom={1.16} zoomTo={1.2} over={50} dark>
        <AnswerScreen headword="Question me on this" body={QUESTION} />
      </PhotoHero>
      <Lower label="What comes back" title="One question. Not an answer." />
    </At>

    <At from={250} to={300}>
      <Crop x={4} y={48} w={520} drift={8}>
        <AnswerScreen headword="Question me on this" body={QUESTION} />
      </Crop>
    </At>
  </AbsoluteFill>
);
