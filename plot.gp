reset
set ylabel 'time(nsec)'
set title 'Performance'
set term png enhanced font 'Verdana,10'
set output 'runtime.png'
set xlabel 'offset'

plot [][] 'plot_data' using 1 with linespoints title 'userspace', \
'' using 2 with linespoints title 'kernelspace', \
'' using 3 with linespoints title 'difference'