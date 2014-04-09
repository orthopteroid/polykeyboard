// crappy poly-spring-keyboard. 2014 orthopteroid@gmail.com
// kudos to Youth Uprising, Perry Cook and the SDL gang
// Apache license
// compile with:
// gcc polykeyboard.c -o polykeyboard -ffast-math -fomit-frame-pointer -msseregparm -mfpmath=sse -msse2 `sdl-config --cflags --libs`

// keyboard rows 1..., q..., a..., z... pluck the springs
// left-shift un/locks the spring constants

#include <stdio.h>
#include <math.h>

#include <SDL/SDL.h>

//#define ENABLE_TONE
//#define ENABLE_SPRING
#define ENABLE_SPRINGSET

//////////////////////////

Sint8 kbd_line( char c, char* line )
{
	Sint16 i = -1;
	while( line[++i] ) { if( line[i] == c ) return i; }
	return -1;
}

//////////////////////////

void synth_mixerCallback(void *userdata, Uint8 *stream, int len);

// of course, size and samples are mere requests and may not be what we get back...
SDL_AudioSpec asDesired =
{
	22050 /*freq*/, AUDIO_U8 /*format*/, 1 /*channels*/, 0 /*silence*/,
	1024 /*samples*/, 0 /*padding*/, 4096 /*size*/, synth_mixerCallback, 0 /*userdata*/
};
SDL_AudioSpec asCurrent;

#define buffers 2 // power of 2
static Uint8 *buffer[ buffers ];

void synth_makeBuffers()
{
	int i = 0;
	for(; i < buffers; i++ )
	{
		buffer[i] = malloc( asCurrent.size );
		memset( buffer[i], 0, asCurrent.size );
	}
}

static Uint32 synth_mixCount = 0;
static Uint32 synth_skipCount = 0;
static Uint32 synth_chopCount = 0;
static Uint32 synth_spinlock_writeBuffer = 0;

void synth_freeBuffers()
{
	printf("%u skips\n", synth_skipCount );
	printf("%u chops\n", synth_chopCount );
	printf("%u mixes\n", synth_mixCount );
	printf("%u waits\n", synth_spinlock_writeBuffer );

	{
		int i = 0;
		for(; i < buffers; i++ )
		{
			free( buffer[i] );
		}
	}
}

static Uint32 synth_readBuffer = buffers - 1;
static Uint32 synth_writeBuffer = 0;

void synth_mixerCallback(void *userdata, Uint8 *stream, int len)
{
	synth_mixCount++;
	if( len != asCurrent.size ) { synth_chopCount++; }
	if( synth_readBuffer - buffers > synth_writeBuffer ) { synth_skipCount++; }
	
	SDL_MixAudio(stream, buffer[ synth_readBuffer & (buffers - 1) ], len, SDL_MIX_MAXVOLUME);
	synth_readBuffer++;
}

//////////////////////////

// sonant.c, (C) 2008-2009 Jake Taylor [ Ferris / Youth Uprising ]
__attribute__((fastcall)) static float osc_sin(float value) {
	float result;
	asm( "fld %1; fsin; fstp %0;" : "=m" (result) : "m" (value) );
	return result;
}
__attribute__((always_inline)) static float osc_square(float value) {
    return osc_sin(value) < 0 ? -1.0f : 1.0f;
}
__attribute__((always_inline)) static float osc_saw(float value) {
    float result;
	asm(
        "fld1;"
        "fld %1;"
        "fprem;"
        "fstp %%st(1);"
        "fstp %0;"
        : "=m" (result)
        : "m" (value)
	);
	return result - .5f;
}
__attribute__((always_inline)) static float osc_tri(float value) {
    float v2 = (osc_saw(value) + .5f) * 4.0f;
    return v2 < 2.0f ? v2 - 1.0f : 3.0f - v2;
}
//////////////////////////

float synth_tone( float dT, Uint32 uT, char c )
{
	static float T = 0.f;
	static float freq = 0.f;
	static float amplitude = 0.f;
	static float duration = 0.f;
	float Q;

	if( c )
	{
		Sint8 key = kbd_line( c, "1234567890" );
		if( key != -1 )
		{
			freq = 400.f + 10.f * key;
			duration = 2.f;
			amplitude = 1.f;
		}
		Q = 0.f;
	}
	else
	{
		T += dT;
		Q = amplitude * osc_square( freq * T );
		if( duration > 0.f )
			duration -= dT;
		else
			amplitude = 0.f;
	}
	return Q;
}

//////////////////////////

// Perry Cook, "Real Sound Synthesis for Interactive Applications", p43
float synth_dampenedspring( float dT, Uint32 uT, char c )
{
	static float Y[3] = { 0.f, 0.f, 0.f };
	static float c1 = 0.f, c2 = 0.f;
	const Uint16 oversamp = 10;

	if( c )
	{
		static float m = .00001f;
		static float k = 10000.f;
		static float r = .0002f;
		Sint8 mi = kbd_line( c, "qwertyuiop" );
		Sint8 ki = kbd_line( c, "asdfghjkl;" );
		Sint8 ri = kbd_line( c, "zxcvbnm,./" );
		if( mi != -1 ) m = .00002f + ((mi - 0) * .0005f);
		if( ki != -1 ) k = 10000.f + ((ki - 5) * 500.f);
		if( ri != -1 ) r = .0002f + ((ri - 5) * .001f);
		{
			float oT = 1. / (asCurrent.freq * oversamp);
			float denom = m + oT*r + oT*oT*k;
			c1 = (2. * m + oT * r) / denom;
			c2 = -m / denom;
		}
		Y[0] = 0.f;
		Y[1] = 1.f;
		Y[2] = 0.f;
	}
	else
	{
		Uint16 i;
		for( i=0; i<oversamp; i++ )
		{
			Y[0] = c1 * Y[1] + c2 * Y[2];
			Y[2] = Y[1];
			Y[1] = Y[0];
		}
	}
	return Y[1] - Y[2];
}

//////////////////////////

// global optimizations don't seem to work with a multithreaded sdl
// app, so we turn them on locally only for the polyphnic-spring-synth.
#pragma GCC push_options
#pragma GCC optimize ("O3")

typedef struct _position { float Y[3]; } position;

float synth_dampenedspringset( float dT, Uint32 uT, char c )
{
	static position spring[4] =
	{
		{ 0.f, 0.f, 0.f },
		{ 0.f, 0.f, 0.f },
		{ 0.f, 0.f, 0.f },
		{ 0.f, 0.f, 0.f },
	};
	static float c1[4] = { 0.f, 0.f, 0.f, 0.f };
	static float c2[4] = { 0.f, 0.f, 0.f, 0.f };
	static float r = .0002f;
	static float k[4] = { 10000.f, 8000.f, 6000.f, 4000.f };
	static int k_lockmode = 0;
	const Uint16 oversamples = 50;

	if( c )
	{
		Sint8 key[4] = {-1,-1,-1,-1};
		key[0] = kbd_line( c, "1234567890" );
		key[1] = kbd_line( c, "qwertyuiop" );
		key[2] = kbd_line( c, "asdfghjkl;" );
		key[3] = kbd_line( c, "zxcvbnm,./" );
		if( c == 0x30 /* left shift */ )
			k_lockmode ^= 1;
		else
		{
			Uint16 s;
			for( s=0; s<4; s++ )
			{
				if( key[s] != -1 )
				{
					float m = .0003f + (key[s] * .000005f);
					float oT = 1.f / (asCurrent.freq * oversamples);
					float denom = m + oT * r + oT * oT * ( k_lockmode ? k[0] : k[s] );
					c1[s] = (2.f * m + oT * r) / denom;
					c2[s] = -m / denom;
					spring[s].Y[0] = 0.f;
					spring[s].Y[1] = 1.f; // plucked weight 1timestep ago
					spring[s].Y[2] = 0.f;
				}
			}
		}
		return 0.f;
	}
	else
	{
		float Q = 0.f;
		Uint16 s;
		for( s=0; s<4; s++ )
		{
			Uint16 i;
			for( i=0; i<oversamples; i++ )
			{
				spring[s].Y[0] = c1[s] * spring[s].Y[1] + c2[s] * spring[s].Y[2];
				spring[s].Y[2] = spring[s].Y[1];
				spring[s].Y[1] = spring[s].Y[0];
			}
			Q += spring[s].Y[1] - spring[s].Y[2];
		}
		return Q;
	}
}

#pragma GCC pop_options

//////////////////////////

static int synth_stop = 0;
static int synth_threadCode = 0;
static int synth_spinlock_onkey = 0;

void synth_onkey( char c )
{
	while( synth_spinlock_onkey ) { ; }
	
#if defined(ENABLE_TONE)

	synth_tone( 0., 0, c );
	
#elif defined(ENABLE_SPRING)

	synth_dampenedspring( 0., 0, c );
	
#elif defined(ENABLE_SPRINGSET)

	synth_dampenedspringset( 0., 0, c );
	
#endif

}

int synth_synthThread(void *ptr)
{
	Uint32 t = 0;
	Uint16 i = 0, o = 0;
	float sqrt2 = sqrt(2.f);
	float quarterpi = 3.14159f / 4.f;
	float twopi = 2.f * 3.14159f;
	float dT = twopi / asCurrent.freq, T = 0.f, D = 3.14159f / 180.f;

	while( !synth_stop )
	{
		synth_spinlock_onkey = 1;
		for( i=0; i<asCurrent.size; i++, t++, T+=dT )
		{
			float Q = 0., S = 0.f;
			
#if defined(ENABLE_TONE)

			Q += synth_tone( dT, i, 0 ); S += 1.;
			
#elif defined(ENABLE_SPRING)

			Q += synth_dampenedspring( dT, i, 0 ); S += 1.;
			
#elif defined(ENABLE_SPRINGSET)

			Q += synth_dampenedspringset( dT, i, 0 ); S += 1.;
			
#endif

			Q /= S;
			buffer[ synth_writeBuffer & (buffers - 1) ][ i ] = (Uint8)( 120.f + 120.f * Q );
		}
		synth_writeBuffer++;
		synth_spinlock_onkey = 0;
		while( synth_readBuffer == synth_writeBuffer ) { synth_spinlock_writeBuffer++; }
	}

    return 0;
}

///////////////////////

int main(int argc, char **argv)
{
	SDL_Thread *thread;
	
	if( SDL_Init( SDL_INIT_AUDIO | SDL_INIT_VIDEO ) != 0 )
	{
		printf("SDL_Init failed\n");
		exit(EXIT_FAILURE);
	}
	
	if( SDL_SetVideoMode( 320, 160, 0, SDL_HWSURFACE ) == NULL )
	{
		printf("SDL_SetVideoMode failed\n");
		exit(EXIT_FAILURE);
	}
	SDL_WM_SetCaption( "A Simple SDL Window", 0 );

	if( SDL_OpenAudio(&asDesired, &asCurrent) != 0 )
	{
		printf("SDL_OpenAudio failed\n");
		exit(EXIT_FAILURE);
	}
	printf("SDL_OpenAudio samples %d\n", asCurrent.samples);
	printf("SDL_OpenAudio size %d\n", asCurrent.size);

	synth_makeBuffers();

	printf("Creating thread...");
    thread = SDL_CreateThread(synth_synthThread, (void *)NULL);
    if( NULL == thread )
    {
        printf("\nSDL_CreateThread failed: %s\n", SDL_GetError());
		exit(EXIT_FAILURE);
    }
	printf("made\n");
    
	printf("Waiting for thread to start...");
    while( !synth_writeBuffer ) { ; }
	printf("started\n");

	SDL_PauseAudio(0);
	
	SDL_Event event;
	while( !synth_stop )
	{
		if( SDL_WaitEvent( &event ) )
		{
			synth_stop = (event.type == SDL_QUIT) | ((event.type == SDL_KEYDOWN) & (event.key.keysym.sym == SDLK_ESCAPE));
			if( event.type == SDL_KEYDOWN )
				synth_onkey( event.key.keysym.sym );
		}
	}

	printf("Waiting for thread to finish...");
	SDL_WaitThread(thread, &synth_threadCode);
	printf("finished\n");

    SDL_CloseAudio();
	SDL_Quit();

	synth_freeBuffers();
	
	return 0;
}
