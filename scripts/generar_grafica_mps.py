#!/bin/python3
import subprocess
import matplotlib.pyplot as plt
import os
import sys
from itertools import groupby
from statistics import mean


def get_puntos(result:str) -> list[tuple[int, float]]:

    result = result.split("\n")

    # Quitar parentesis.
    lines: list[str] = [line.replace("(", "").replace(")", "") for line in result]

    # Dividir en columnas.
    lines = [[x for x in line.split(" ") if x != ""] for line in lines]
    
    # Sacar indices de las columnas que nos interesan.
    ind_titulos = [x[0] for x in enumerate(result) if x[1].startswith("%")][-2]
    nombres_columnas = [x for x in lines[ind_titulos] if x != "Gflop/s"][1:]
    ind_N = nombres_columnas.index('N')
    ind_CUBLAS = nombres_columnas.index('cuBLAS')
    # Quitar lineas que empiezan por %.
    lines: list[str]  = [line for line in lines if len(line) > 1 and line[0] != '%']
    puntos = [(k, mean([float(cols[ind_CUBLAS]) for cols in list(g)])) for k, g in groupby(lines, lambda l: int(l[ind_N]))]
 
    return puntos

def aplicar_configuracion(conf: str) -> list[tuple[str, str]]: 

    def get_devices() -> list[tuple[str, str]]:

        mig_enabled = len(subprocess.run('nvidia-smi | grep "MIG devices"', shell=True, stdout=subprocess.PIPE).stdout.decode("utf-8")) > 0
        
        devices = subprocess.run('nvidia-smi -L', shell=True, stdout=subprocess.PIPE).stdout.decode("utf-8").split("\n")[:-1]
        devices = [dev.replace("(", "").replace(")", "") for dev in devices]

        devices = [[x for x in dev.split(" ") if x != ""] for dev in devices]
        if mig_enabled:
            devices = [(dev[1], dev[-1]) for dev in devices if dev[0].startswith("MIG")]
        else:
            devices = [(f"{dev[2]} {dev[3]}", dev[-1]) for dev in devices if dev[0].startswith("GPU")]
        
        return devices

    subprocess.run(f"sudo nvidia-mig-parted apply -f /opt/nvidia/ocejon.yaml -c {conf}", shell=True)
    return get_devices()
    
def generar_grafica(configuracion: str, comando: list[str]):

    
    os.mkdir(configuracion)
    os.chdir(configuracion)

    plt.title(configuracion)
    plt.xlabel("N")
    plt.ylabel("GFLOPS")
    plt.grid()

    device = aplicar_configuracion(configuracion)[0]
    popens = []
  
    for comando in comandos:

        popen = subprocess.Popen(f"CUDA_MPS_PIPE_DIRECTORY=/tmp/nvidia-mps CUDA_VISIBLE_DEVICES={device[1]} {comando}", shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        popens.append(popen)

    for ind, (comando, popen) in enumerate(zip(comandos, popens)):
        os.waitpid(popen.pid, 0)
        result = popen.stdout.read().decode("utf-8")
        puntos = get_puntos(result)

        x, y = zip(*puntos)

        label = comando.split("/")[-1]
        plt.plot(x, y, label=label)         

        with open(f"{ind}-{label}", "w") as f:
            f.write(result)

    plt.legend()
    plt.savefig(f"{configuracion}.png".replace(" ", "_"))

if __name__ == "__main__":

    mig_configs = sys.argv[1].split(",")
    comandos = (" ".join(sys.argv[2:])).split(",")
    if len(mig_configs) == 1:
        generar_grafica(mig_configs[0], comandos)
    else:
        print("Este script solo soporta una configuración.")

