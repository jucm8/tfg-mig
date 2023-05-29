#!/bin/python3
from getopt import getopt
import subprocess
import sys
from statistics import mean
from itertools import groupby
import matplotlib.pyplot as plt
import os

# Función para aplicar una configuración MIG.
# Recibe como argumento el nombre de la configuración, y devuelvela lista de tuplas (ID, nombre) para cada uno de los devices
# que resulten de aplicar dicha configuración las cuales obtiene del comando nvidia-smi -L.
def aplicar_configuracion(conf: str) -> list[tuple[str, str]]: 

    def get_devices() -> list[tuple[str, str]]:

        mig_enabled = len(subprocess.run('nvidia-smi | grep "MIG devices"', 
        shell=True, stdout=subprocess.PIPE).stdout.decode("utf-8")) > 0
        
        devices = subprocess.run('nvidia-smi -L', shell=True, stdout=subprocess.PIPE).stdout.decode("utf-8").split("\n")[:-1]
        devices = [dev.replace("(", "").replace(")", "") for dev in devices]

        devices = [[x for x in dev.split(" ") if x != ""] for dev in devices]

        if mig_enabled:
            devices = [(dev[1], dev[-1]) for dev in devices if dev[0].startswith("MIG")]
        else:
            devices = [(f"{dev[2]} {dev[3]}", dev[-1]) 
                       for dev in devices if dev[0].startswith("GPU")]

        return devices

    subprocess.run(f"sudo nvidia-mig-parted apply -f /opt/nvidia/ocejon.yaml -c {conf}", shell=True)
    return get_devices()

# Función que procesa la salida de los tests de magma, y devuelve una lista de puntos para dibujar las gráficas.
def get_puntos(result: str) -> list[tuple[int, float]]:

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

def grafica_varias_configuraciones(configs: list[str], comando: str):
    
    title = comando.split("/")[-1]

    new_dir = f"{'_'.join(configs)} - {title.replace('testing_', '')}"
    os.mkdir(new_dir)
    os.chdir(new_dir)

    plt.title(title)
    plt.xlabel("N")
    plt.ylabel("GFLOPS")
    plt.grid()

    for config in configs:
        devices = aplicar_configuracion(config)

        for dev in devices:
            print(f"Lanzando en {config}...")
            result = subprocess.run(f"CUDA_VISIBLE_DEVICES={dev[1]} {comando}", shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout.decode("utf-8")
            with open(f"{config}.txt", "w") as file:
                file.write(result)
                
            puntos = get_puntos(result)
            x, y = zip(*puntos)
            plt.plot(x, y, label=config)

    plt.legend()
    plt.savefig("grafica.png")

def grafica_una_configuracion(configuracion: str, comando: str):

    title = comando.split("/")[-1]
    
    new_dir = f"{configuracion} - {title.replace('testing_', '')}"
    os.mkdir(new_dir)
    os.chdir(new_dir)

    plt.title(title)
    plt.xlabel("N")
    plt.ylabel("GFLOPS")
    plt.grid()

    devices = aplicar_configuracion(configuracion)

    popens = []
    for dev in devices:
        
        popen = subprocess.Popen(f"CUDA_VISIBLE_DEVICES={dev[1]} {comando}", shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        popens.append(popen)

    for ind, popen in enumerate(popens):

        os.waitpid(popen.pid, 0)
        result = popen.stdout.read().decode("utf-8")
        with open(f"{devices[ind][0]}.txt", "w") as file:
            file.write(result)
        puntos = get_puntos(result)

        x, y = zip(*puntos)
        plt.plot(x, y, label=devices[ind][0])         

    plt.legend()
    plt.savefig(f"{configuracion}_{title.replace('testing_', '')}.png".replace(" ", "_"))

def grafica_una_configuracion_varios_comandos(configuracion: str, comandos: list[str]):

    devices = aplicar_configuracion(configuracion)
    if len(devices) != len(comandos):
        print("El número de comandos tiene que ser el mismo que el de devices en la configuracion.")
        return

    title = " - ".join([comando.split("/")[-1].split(" ")[0] for comando in comandos])
    
    new_dir = f"{configuracion} - {title.replace('testing_', '')}"
    os.mkdir(new_dir)
    os.chdir(new_dir)

    plt.title(title)
    plt.xlabel("N")
    plt.ylabel("GFLOPS")
    plt.grid()
    
    popens = []
    for comando, dev in zip(comandos, devices):
        
        popen = subprocess.Popen(f"CUDA_VISIBLE_DEVICES={dev[1]} {comando}", shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        popens.append(popen)

    for ind, popen in enumerate(popens):

        os.waitpid(popen.pid, 0)
        result = popen.stdout.read().decode("utf-8")
        with open(f"{ind} - {devices[ind][0]}.txt", "w") as file:
            file.write(result)
        puntos = get_puntos(result)

        x, y = zip(*puntos)
        label = comandos[ind].split('/')[-1].replace('testing_', '').split(" ")[0]
        plt.plot(x, y, label=f"{devices[ind][0]} - {label}")         

    plt.legend()
    plt.savefig(f"{configuracion}_{title.replace('testing_', '')}.png".replace(" ", "_"))

if __name__ == "__main__":

    mig_configs = sys.argv[1].split(",")
    comandos = (" ".join(sys.argv[2:])).split(",")

    if len(comandos) == 1 and len(mig_configs) == 1:
        # 1 test en 1 configuracion de n devices.
        grafica_una_configuracion(mig_configs[0], comandos[0])
    elif len(comandos) == 1 and len(mig_configs) > 1:
        # 1 test en varias configuraciones de un solo device.
        grafica_varias_configuraciones(mig_configs, comandos[0])
    elif len(comandos) > 1 and len(mig_configs) == 1:
        # n tests distintos en 1 configuracion de n devices.
        grafica_una_configuracion_varios_comandos(mig_configs[0], comandos)
    else:
        print("Número de tests y configuraciones incompatible.")