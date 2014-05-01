#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <jack/jack.h>
#include <jack/midiport.h>

jack_port_t *input_port;
jack_port_t *output_port;
jack_client_t *client;

struct options {
	int weird;
	int sustained;
	int e3d;
};

int process(jack_nframes_t nframes, struct options *opts)
{
	jack_midi_event_t ebuf;

	jack_position_t position;

	void *in_buf = jack_port_get_buffer(input_port, nframes);
	void *out_buf = jack_port_get_buffer(output_port, nframes);
	jack_midi_clear_buffer(out_buf);

	jack_nframes_t numevents = jack_midi_get_event_count(in_buf);
	jack_transport_query(client, &position);

	for(unsigned int i = 0; i < numevents; ++i) {
		jack_midi_event_get(&ebuf, in_buf, i);

		unsigned char *x = ebuf.buffer;
		if (ebuf.size == 3) {
			if (x[0] == 0x90 && x[1] == 0x40) {
				// Unless you reeeally hammered the E3,
				// this is the sustain pedal.
				if (x[2] == 0x7f) {
					x[0] = 0xb0;
					opts->sustained = 1;
				} else { // Otherwise...
					opts->e3d = 1;
				}
			} else if (x[0] == 0x80 && x[1] == 0x40) {
				if (opts->sustained && opts->e3d) {
					// We have an unknown situation!
					// Did you just release E3 or the
					// sustain pedal? The sanest thing
					// to do is to release E3, but you
					// may have a weird playstyle.
					if (opts->weird) {
						x[0] = 0xb0;
						x[2] = 0;
						opts->sustained = 0;
					} else {
						opts->e3d = 0;
					}
				} else if (opts->sustained) {
					// No question.
					x[0] = 0xb0;
					x[2] = 0;
					opts->sustained = 0;
				} else {
					opts->e3d = 0;
				}
			} else if (x[0] == 0xb0 && x[1] == 0x40) {
				// And sometimes it's actually right.
				if (x[2] > 0x40) {
					opts->sustained = 1;
				} else {
					x[2] = 0;
					opts->sustained = 0;
				}
			}
		}

		jack_midi_event_write(out_buf, 0, ebuf.buffer, ebuf.size);
	}

	return 0;
}

void error(const char *desc)
{
	fprintf(stderr, "JACK error: %s\n", desc);
}

void jack_shutdown(void *arg)
{
	assert(arg == 0);
	exit(1);
}

int main(int argc, char **argv)
{
	struct options opts;

	opts.weird = 0;
	opts.e3d = 0;
	opts.sustained = 0;

	if (argc >= 2) {
		if (strcmp(argv[1], "--weird") == 0) {
			printf("Weird mode engange!\n");
			opts.weird = 1;
		} else {
			fprintf(stderr, "Usage: $0 [--weird]\n");
			fprintf(stderr, "Weird means release sustain if "
					"indestinguishable from e3.\n");
			exit(1);
		}
	}
	
	jack_set_error_function(error);

	if ((client = jack_client_open("midiunfucker",
					JackNoStartServer,
					NULL)) == 0) {
		fprintf(stderr, "no jackd?\n");
		return 1;
	}

	jack_set_process_callback(client, (JackProcessCallback) process, &opts);
	jack_on_shutdown(client, jack_shutdown, 0);

	input_port = jack_port_register (client, "input",
			JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	output_port = jack_port_register (client, "output", 
                     JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

	if (jack_activate(client)) {
		fprintf(stderr, "cannot activate client\n");
		return 1;
	}

	if (jack_connect(client, "system:midi_capture_2",
				jack_port_name(input_port))) {
		fprintf(stderr, "no conn input\n");
	}

	if (jack_connect(client, jack_port_name(output_port), "qsynth:midi")) {
		fprintf(stderr, "no conn output\n");
	}

	for(;;)
		sleep(1);

	jack_client_close(client);
	return 0;
}
