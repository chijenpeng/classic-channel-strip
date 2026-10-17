// The editor and the processor without a host or a screen.
//
//   classic_channel_snapshot out.png [id=value ...]                the editor at 700 x 670 (1x), rendered at 2x pixel scale
//   classic_channel_snapshot --size w h out.png [id=value ...]     the same at another window size (scaling)
//   classic_channel_snapshot --curve [id=value ...]                measured card response at 1/12-octave points,
//                                                      20 Hz - 20 kHz (compare classic_channel_measure)
//   classic_channel_snapshot --render in.wav out.wav [id=value ...]
//                                                      a wav through processBlock at the file's rate,
//                                                      mono or stereo, blocks of 512; 24-bit out, the
//                                                      2x latency (4 samples) removed so it lines up
//
// Settings by parameter id in the parameter's units (ids as the session's: input, hpf_on, hpf_freq,
// ..., eq_on, output, oversample, dyn_on, comp_*, gate_*, dyn_position, sidechain_source;
// switches/choices use their zero-based index. --rate fs runs at fs instead of 48 kHz.
// The PNG modes also play half a second of noise at -12 dBFS so the meters show a level (--quiet:
// not), and warn about any hover help that needs more than its three lines.

#include "../Source/PluginEditor.h"
#include "../Source/ResponseAnalyser.h"

#include <cstdio>

namespace
{
int fail (const juce::String& message)
{
    std::fprintf (stderr, "%s\n", message.toRawUTF8());
    return 1;
}

bool applySettings (Ssl4000EqProcessor& p, const juce::StringArray& settings)
{
    for (const auto& s : settings)
    {
        const auto id = s.upToFirstOccurrenceOf ("=", false, false);
        auto* param = p.apvts.getParameter (id);
        if (param == nullptr || ! s.containsChar ('='))
        {
            std::fprintf (stderr, "unknown setting: %s\n", s.toRawUTF8());
            return false;
        }
        param->setValueNotifyingHost (param->convertTo0to1 (s.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
    }
    return true;
}

/** The response analyser is a developer tool; the plug-in editor no longer runs it. */
bool measureCurve (Ssl4000EqProcessor& p, ResponseAnalyser::Curve& curve)
{
    ResponseAnalyser analyser;
    ResponseAnalyser::Settings settings;
    for (size_t i = 0; i < settings.size(); ++i)
        settings[i] = p.apvts.getRawParameterValue (ar::fx::kSslParamIds[i])->load();
    const int generation = analyser.request (settings, p.getSampleRate() * p.getActiveOversampling());
    for (int i = 0; i < 1000; ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        if (analyser.fetch (curve) && curve.generation == generation)
            return true;
    }
    return false;
}

/** Half a second of white noise at -12 dBFS peak through processBlock, the editor's timer
    running in between, so the meters have something to show. */
void playNoise (Ssl4000EqProcessor& p)
{
    juce::Random random (7);
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;
    const int blocks = juce::roundToInt (0.5 * p.getSampleRate() / 512.0);
    for (int b = 0; b < blocks; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                block.setSample (ch, i, 0.25f * (random.nextFloat() * 2.0f - 1.0f));
        p.processBlock (block, midi);
        if (b % 3 == 0)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
}

int render (const juce::File& in, const juce::File& out, const juce::StringArray& settings)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (in));
    if (reader == nullptr)
        return fail ("cannot read " + in.getFullPathName());
    const int channels = (int) reader->numChannels;
    if (channels != 1 && channels != 2)
        return fail ("mono or stereo only (" + juce::String (channels) + " channels)");
    const double rate = reader->sampleRate;
    const int length = (int) reader->lengthInSamples;

    Ssl4000EqProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.inputBuses.add (set);
    layout.outputBuses.add (set);
    p.setBusesLayout (layout);
    if (! applySettings (p, settings))
        return 1;
    p.setPlayConfigDetails (channels, channels, rate, 512);
    p.prepareToPlay (rate, 512);
    const int latency = p.getLatencySamples();

    juce::AudioBuffer<float> audio (channels, length + latency);
    audio.clear();
    reader->read (&audio, 0, length, 0, true, channels > 1);
    const float inPeak = audio.getMagnitude (0, length);
    juce::MidiBuffer midi;
    for (int start = 0; start < audio.getNumSamples(); start += 512)
    {
        juce::AudioBuffer<float> block (audio.getArrayOfWritePointers(), channels, start, juce::jmin (512, audio.getNumSamples() - start));
        p.processBlock (block, midi);
    }

    out.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (out);
    if (! dynamic_cast<juce::FileOutputStream&> (*stream).openedOk())
        return fail ("cannot write " + out.getFullPathName());
    auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate)
                                                                                                  .withNumChannels (channels)
                                                                                                  .withBitsPerSample (24));
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (audio, latency, length))
        return fail ("cannot write " + out.getFullPathName());
    std::printf ("%s: %d samples, %d channel(s) at %.0f Hz, oversampling %dx (latency %d removed); peak %+.2f dBFS in, %+.2f dBFS out\n",
                 out.getFileName().toRawUTF8(), length, channels, rate, p.getActiveOversampling(), latency,
                 juce::Decibels::gainToDecibels (inPeak, -200.0f), juce::Decibels::gainToDecibels (audio.getMagnitude (latency, length), -200.0f));
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI gui;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto usage = "usage: classic_channel_snapshot out.png [id=value ...] | --size w h out.png [...] | --curve [...] | --render in.wav out.wav [...]";

    if (args[0] == "--render")
    {
        if (args.size() < 3)
            return fail (usage);
        return render (cwd.getChildFile (args[1]), cwd.getChildFile (args[2]), juce::StringArray (args.begin() + 3, args.size() - 3));
    }

    bool curveOnly = false, quiet = false;
    int width = EditorView::kWidth, height = EditorView::kHeight;
    double rate = 48000.0;
    juce::File png;
    juce::StringArray settings;
    for (int i = 0; i < args.size(); ++i)
    {
        if (args[i] == "--curve")                           curveOnly = true;
        else if (args[i] == "--quiet")                      quiet = true;
        else if (args[i] == "--rate" && i + 1 < args.size()) rate = args[++i].getDoubleValue();
        else if (args[i] == "--size" && i + 3 < args.size())
        {
            width = args[++i].getIntValue();
            height = args[++i].getIntValue();
            png = cwd.getChildFile (args[++i]);
        }
        else if (args[i].containsChar ('='))                settings.add (args[i]);
        else if (png == juce::File())                       png = cwd.getChildFile (args[i]);
        else return fail (usage);
    }
    if (! curveOnly && png == juce::File())
        return fail (usage);

    Ssl4000EqProcessor p;
    if (! applySettings (p, settings))
        return 1;
    p.setPlayConfigDetails (2, 2, rate, 512);
    p.prepareToPlay (rate, 512);

    if (curveOnly)
    {
        ResponseAnalyser::Curve curve;
        if (! measureCurve (p, curve))
            return fail ("the response measurement did not finish");
        std::printf ("#  Hz        dB   (card at %.0f Hz)\n", curve.sampleRate);
        for (int i = 0; i <= 120; ++i)
        {
            const double f = 20.0 * std::pow (2.0, i / 12.0);
            if (f > 20001.0 || f >= 0.5 * curve.sampleRate)
                break;
            std::printf ("%8.1f %+8.3f\n", f, ResponseAnalyser::dbAt (curve, f));
        }
        return 0;
    }

    Ssl4000EqEditor editor (p);
    editor.setSize (width, height);
    const auto& view = editor.getView();
    if (! quiet)
        playNoise (p);

    int warnings = 0;
    for (const auto& [title, text] : view.allHelp())
        if (const int lines = view.helpLinesNeeded (text); lines > 3)
        {
            std::fprintf (stderr, "warning: the help for %s needs %d lines\n", title.toRawUTF8(), lines);
            ++warnings;
        }

    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
    png.deleteFile();
    juce::FileOutputStream out (png);
    if (! out.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, out))
        return fail ("cannot write " + png.getFullPathName());
    std::printf ("%s: %d x %d window, %d x %d pixels; %d help warning(s)\n", png.getFileName().toRawUTF8(),
                 editor.getWidth(), editor.getHeight(), image.getWidth(), image.getHeight(), warnings);
    return 0;
}
