import sys
import subprocess
import numpy as np
import matplotlib.pyplot as plt
import os

runs = 50

def outlier_filter(datas, threshold = 2):
    datas = np.array(datas)
    z = np.abs((datas - datas.mean()) / datas.std())
    return datas[z < threshold]

def data_processing(data_set, n):
    catgories = data_set[0].shape[0]
    samples = data_set[0].shape[1]
    final = np.zeros((catgories, samples))

    for c in range(catgories):        
        for s in range(samples):
            final[c][s] =                                                    \
                outlier_filter([data_set[i][c][s] for i in range(n)]).mean()
    return final

def measure():
    Ys = []
    fileout = 'tmp.txt'
    for i in range(runs):
        try:
            comp_proc = subprocess.run(f'sudo taskset -c 7 ./client > {fileout}', shell = True, check = True)
            output = np.loadtxt(fileout, dtype = 'float').T
            Ys.append(np.delete(output, 0, 0))
        except Exception as e:
            print(e)
            exit(1)
    X = output[0]
    Y = data_processing(Ys, runs)
    os.remove(fileout)
    
    return [X, Y]

if __name__ == "__main__":
    method = 0
    if len(sys.argv) == 2:
        method = int(sys.argv[1])
    data = measure()
    X = data[0]
    Y = data[1]

    np.savetxt('plot_data', Y.T, delimiter=' ')
    
    '''
    fig, ax = plt.subplots(1, 1, sharey = True)
    ax.set_title('fibonacci performance', fontsize = 16)
    ax.set_xlabel(r'$n_{th}$ fibonacci', fontsize = 16)
    ax.set_ylabel('time (ns)', fontsize = 16)

    ax.plot(X, Y[0], marker = '*', markersize = 3, label = 'user')
    ax.plot(X, Y[1], marker = '+', markersize = 7, label = 'kernel')
    ax.plot(X, Y[0] - Y[1], marker = '^', markersize = 3, label = 'syscall')
    ax.legend(loc = 'upper left')

    plt.show()
    '''
