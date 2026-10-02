import pandas as pd
import numpy as np

def calcular_mre_exacto(ataque_nombre):

    print(f" MRE NÚCLEO ESTRICTO (|J| ~ 8 ventanas): {ataque_nombre.upper()}")
    
    exact_file = f"exact_{ataque_nombre}.csv"
    try:
        exact_df = pd.read_csv(exact_file)
    except Exception as e:
        print(f"Error leyendo {exact_file}: {e}")
        return

    # Delimitar intervalo del ataque 
    filas_ataque = exact_df[exact_df['exact_f'] > 1000]
    if filas_ataque.empty:
        print("No se encontró ataque.")
        return
        
    idx_inicio = filas_ataque.index.min()
    idx_fin = filas_ataque.index.max()
    
    # Conjunto J desde el inicio hasta el fin del ataque, con f > 0
    j_subset = exact_df.loc[idx_inicio:idx_fin]
    j_indices = j_subset[j_subset['exact_f'] > 0].index.tolist()
    
    print(f"-> Rango de ventanas en J (Total: {len(j_indices)} ventanas): {j_indices}")

    widths = [256, 1024, 4096]
    algoritmos = [('cms', 'Count-Min Sketch'), ('cs', 'CountSketch')]

    for alg_key, alg_nombre in algoritmos:
        for w in widths:
            try:
                df = pd.read_csv(f"{ataque_nombre}_{alg_key}_{w}.csv")
                
                errores_ventanas = []
                for j in j_indices:
                    f_exacta = exact_df.loc[j, 'exact_f']
                    f_estimada = df.loc[j, 'estimate']
                    
                    if f_exacta > 0:
                        err_relativo = abs(f_estimada - f_exacta) / f_exacta
                        errores_ventanas.append(err_relativo)
                
                mre = np.mean(errores_ventanas) if errores_ventanas else 0
                print(f"{alg_nombre:<20} (w={w:<4}) | MRE Promedio: {mre:.6f}")
                print(f"Errores por ventana en J: {[round(e, 4) for e in errores_ventanas]}")
            except Exception as e:
                print(f"Error leyendo archivo para w={w}")

calcular_mre_exacto("ddos")
calcular_mre_exacto("scan")
