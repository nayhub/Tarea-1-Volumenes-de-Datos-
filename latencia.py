import pandas as pd

def calcular_latencia_correcto(ataque_nombre, threshold=1000):
    print(f" LATENCIA REAL (ZONA DE ATAQUE): {ataque_nombre.upper()}")
    
    exact_file = f"exact_{ataque_nombre}.csv"
    try:
        exact_df = pd.read_csv(exact_file)
    except Exception as e:
        print(f"Error leyendo {exact_file}: {e}")
        return
    
    # Rango exacto del ataque
    filas_ataque = exact_df[exact_df['exact_f'] > threshold]
    if filas_ataque.empty:
        print("No se encontró inicio de ataque en exacto.")
        return
    
    idx_exact_start = filas_ataque.index.min()
    print(f"Referencia Exacta inicia en ventana: {idx_exact_start} (t = {idx_exact_start * 10}s)")

    widths = [256, 1024, 4096]
    algoritmos = [('cms', 'Count-Min Sketch'), ('cs', 'CountSketch')]

    # Buscar en una ventana de búsqueda cercana al ataque
    zona_busqueda_inicio = max(0, idx_exact_start - 10)

    for alg_key, alg_nombre in algoritmos:
        for w in widths:
            try:
                df = pd.read_csv(f"{ataque_nombre}_{alg_key}_{w}.csv")
                # Filtrar para evitar ruido lejano
                zona_df = df.iloc[zona_busqueda_inicio:]
                
                sketch_triggers = zona_df[zona_df['estimate'] > threshold].index
                
                if not sketch_triggers.empty:
                    idx_sketch_start = sketch_triggers[0]
                    diff_windows = idx_sketch_start - idx_exact_start
                    
                    print(f"{alg_nombre:<20} (w={w:<4}) | Ventana {idx_sketch_start} | Diferencia: {diff_windows:+d} vent.")
                else:
                    print(f"{alg_nombre:<20} (w={w:<4}) | No superó el umbral en la zona")
            except Exception:
                print(f"No se pudo leer {ataque_nombre}_{alg_key}_{w}.csv")

calcular_latencia_correcto("ddos")
calcular_latencia_correcto("scan")
