all:
	gcc -g -o streamerd streamerd.c
	gcc -g -o playaudio playaudio.c -l asound -l pthread
	gcc -g -o testaudio testaudio.c -l asound -l pthread

clean:
	rm streamerd
	rm playaudio
	rm testaudio
