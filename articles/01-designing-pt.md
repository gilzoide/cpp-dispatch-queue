# Design de Dispatch Queue
> 2026-09-12 | `#C++ #Multithreading #ThreadPool` | [*English version*](01-designing-en.md)

## Contexto
Eu trabalho com jogos já faz um tempo e nos últimos 2 anos pude trabalhar com alguns jogos nativos que usam motores feitos em C++.
Todos têm algum tipo de implementação de *thread pool* / despachador de tarefas, apesar de alguns projetos usarem *multithreading* mais (e de maneira melhor) do que outros.
*Multithreading* é um rolê bem complexo, mas pode ser mais simples com boas abstrações.

Também uso Unity há bastante tempo e gosto muito do [async/await](https://learn.microsoft.com/pt-br/dotnet/csharp/asynchronous-programming/task-asynchronous-programming-model) e da [Task Parallel Library](https://learn.microsoft.com/pt-br/dotnet/standard/parallel-programming/task-parallel-library-tpl) do C#.
O [Job System da Unity](https://docs.unity3d.com/Manual/job-system-overview.html) também é maneiro, apesar de ser mais difícil de usar principalmente por exigir tipos não gerenciados (*unmanaged*).

Então a ideia desse projeto aqui é tentar implementar um despachador de tarefas *multithreaded* robusto com uma API bem bacana que:

1. Dependa somente da STL do C++, usando primitivas de *threading* do C++11 e evitando código dependente de plataforma o máximo possível
2. Suporte funcionalidades maneiras do C++ como [lambdas](https://en.cppreference.com/cpp/language/lambda) e [corrotinas](https://en.cppreference.com/cpp/language/coroutines) (C++20).
3. Permitir um estilo de código assíncrono que uso na Unity / C# e gosto muito, por exemplo com funcionalidade similar ao que [`Task.WhenAll`](https://learn.microsoft.com/pt-br/dotnet/api/system.threading.tasks.task.whenall) e [`TaskCompletionSource`](https://learn.microsoft.com/pt-br/dotnet/api/system.threading.tasks.taskcompletionsource) provêm.
4. Ser agnóstico de *engine*: não importa se estiver usando Cocos2d, Raylib, SDL ou qualquer outra coisa, Dispatch Queue ainda é útil.
5. Ser compatível com [Emscripten](https://emscripten.org/) e builds pra web.

Durante o desenvolvimento eu acabei adicionando mais algumas escolhas de *design* que vou comentar abaixo.


## Subclasse por tarefa
Uma implementação que encontrei em um projeto exige que você crie uma subclasse para cada tarefa que quiser executar.
O Job System da Unity e outras implementações que encontrei online também fazem isso.

Mas eu sinto que o "jeito C++" seria usando [functors](https://en.cppreference.com/cpp/utility/functional) e/ou [`std::function`](https://en.cppreference.com/cpp/utility/functional/function).
Exigir que usuários criem subclasses somente para ter um método `void Execute()` parece demais pra mim, eu queria uma API o mais simples possível, mas ainda sim efetiva.
Suportar *lambdas* é muito sucesso em termos de usabilidade na minha opinião.

Saca só [`std::async`](https://en.cppreference.com/cpp/thread/async) do C++ por exemplo, você passa um *functor* e seus argumentos opcionais, ele torna um [`std::future`](https://en.cppreference.com/cpp/thread/future) que pode ser usado para acessar o resultado depois.
Simples, poderoso, maravilhoso!

Aqui como se dispacha uma tarefa usando Dispatch Queue:
```cpp
task_dispatcher dispatcher;

dispatcher.dispatch([]() {
    std::cout << "Olá mundo!" << std::endl;
});
```


## Future
Só que algumas coisa me incomodam bastante em `std::future`:

1. O único jeito de descobrir se o valor do *future* está prontamente disponível é usando [`wait_for`](https://en.cppreference.com/cpp/thread/future/wait_for) com um *timeout* zero.
   Algo como um método `future.is_ready()` seria bem melhor em termos de API na minha opinião.
2. O único jeito de descobrir se a tarefa falhou, se rolou alguma exceção, é chamando [`get`](https://en.cppreference.com/cpp/thread/future/get) dentro de um bloco try/catch, não há nada tipo `future.is_failed()` ou `future.get_exception()`.
3. Falta um jeito bom de executar código logo após o *future* ser concluído, assincronamente, algo parecido com [`Task.ContinueWith`](https://learn.microsoft.com/pt-br/dotnet/api/system.threading.tasks.task.continuewith) do C#.

Comecei a implementação usando `std::future` e `std::packaged_task`, mas acabei implementando meu próprio [`task_future<T>`](../include/dispatch_queue/detail/task_future.hpp) que resolve os problemas acima.


```cpp
task<void> t1 = dispatcher.dispatch([]() {
    std::cout << "Olá mundo!" << std::endl;
});

// Avalie o estado usando `task.get_state()`
if (t1.get_state() == task_state::pending) {
    std::cout << "Não acabou ainda" << std::endl;
}

// Adicione uma continuação usando `task.then()`
t1.then([](const task<void>& t) {
    if (t.get_state() == task_state::ready) {
        std::cout << "Foi sucesso =D" << std::endl;
    }
    else {  // task_state::failed
        std::cout << "Falhou =(" << std::endl;
    }
})
```


## Promise
Mencionei o `TaskCompletionSource` do C# acima, xo explicar melhor: é um meio de usuários completarem tarefas "manualmente".
É muito útil para converter métodos que recebem *callbacks* em código pronto pra ser usado com async/await.

Em Dispatch Queue, decidi adicionar essa funcionalidade na classe `task<T>` mesmo ao invés de criar uma classe `task_source<T>` separada, mais por simplicidade mesmo.
```cpp
// Usando tasks como promise
task<int> promise = task<int>::create_pending();
do_something_async_with_callback([=](int result) {
    promise.set_value(result);
});

int result = promise.get();
```


## Tasks agregadas
É muito útil poder esperar várias tarefas completarem de uma vez só.
Pra isso `when_all` existe:
```cpp
// Inicializa vários sistemas do jogo concorrentemente
when_all(
    dispatcher.dispatch(initialize_graphics),
    dispatcher.dispatch(initialize_audio),
    dispatcher.dispatch(initialize_input),
    dispatcher.dispatch(initialize_analytics_service),
    dispatcher.dispatch(load_saved_game)
).then([](const task<void>& t) {
    // Quando tudo termina, começa o jogo
    start_game();
})
```


## Sincronizando com o *main loop*
Outro padrão muito comum em aplicações interativas / jogos é despachar uma tarefa para o *background* e, após finalizada, usar o resultado da tarefa na *main thread*.

Em Unity, assim como Cocos2d e outros motores, algumas APIs não são *thread-safe* e só podem ser usados na *main thread*, como por exemplo atualizar gráficos e adicionar objetos na árvore da cena.

Ler e escrever arquivos de *save*, esperar requisições HTTP e *parsear* arquivos grandes de configuração JSON ou XML são exemplos de tarefas potencialmente longas que você vai querer rodar em *background* para evitar travar os quadros do jogo.

Decisão de *design* adicional:
- Poder despachar tarefas na *main thread* da aplicação

```cpp
dispatcher.dispatch_main([]() {
    std::cout << "No main loop" << std::endl;
});

void app_main_loop() {
    // Chame no main loop do seu app...
    dispatcher.main_loop();
}
```


## Executando tarefas após um *delay*
Outro padrão super comum é executar tarefas após um tempo determinado.
Vi isso em algumas implementações e pensei "Por que não?", mesmo que ainda não tenha usado pessoalmente em nenhum projeto 😅

Decisão de *design* opcional adicional:
- Poder despachar tarefas na *main thread* da aplicação com um *delay*

```cpp
float delay_secs = 1;
dispatcher.dispatch_main_after(delay_secs, []() {
    std::cout << "No main loop após 1s" << std::endl;
});

void app_main_loop(float dt) {
    // Chame no main loop do seu app...
    // Precisa do delta tempo para avançar o timer interno
    dispatcher.main_loop(dt);
}
```


## *Task pinning*
Esse não é muito comum, mas é bem interessante.
Outro padrão que existe é permitir que algumas tarefas sejam pinadas em *threads* específicas.
Isso é útil para serializar tarefas, como por exemplo salvar arquivos de um jeito que eles nunca são acessados concorrentemente, evitando corrompê-los.
Também é útil caso a tarefa necessite que variáveis *thread-local* sejam preservadas entre execuções.

Executar algumas tarefas sempre na mesma *thread* é facil de fazer usando despachadores de tarefa com 1 *thread* só.

Sendo assim, escolhi uma opção um pouco diferente: ao invés de pinar tarefas em *threads* específicas, fiz um jeito de garantir que tarefas usando a mesma tag (um número inteiro) nunca sejam executadas em paralelo.
Isso permite que tarefas com tags rodem em quaisquer das *threads* do *pool*.
Usuários que precisarem de tarefas sendo executadas em *threads* específicas podem simplsmente criar despachadores seriais separados.

```cpp
enum TaskTags {
    SAVE_FILE_IO,
};

// As três tarefas a seguir executarão uma só de cada vez,
// mesmo que o despachador tiver mais threads esperando novas tarefas
dispatcher.dispatch_tagged(SAVE_FILE_IO, [](){ /* ... */ });
dispatcher.dispatch_tagged(SAVE_FILE_IO, [](){ /* ... */ });
dispatcher.dispatch_tagged(SAVE_FILE_IO, [](){ /* ... */ });
```


## Corrotinas (C++20)
C++20 introduz corrotinas, que são um mecanismo bem dahora para suspender e continuar funções assincronamente, similar a async/await do C# (e do Python e JavaScript).

Só que no C++ não há um tempo de execução padrão para essa suspensão e continuação das funções, nós precisamos prover alguma implementação para isso funcionar.

Então usei [`task_dispatcher`](../include/dispatch_queue/task_dispatcher.hpp) e [`task<T>`](../include/dispatch_queue/task.hpp) para implementar o tempo de execução de corrotinas, permitindo código tipo o seguinte:
```cpp
task_dispatcher dispatcher;

// Se sua função retorna task<T> você pode usar `co_await` e `co_return`,
// efetivamente transformando-a numa corrotina
task<int> my_coroutine() {
    // Usar `co_await` suspende a corrotina
    // Ela será automaticamente continuada usando `task.then()`
    co_await dispatcher.dispatch([]() { /* ... */ });

    // Tasks com valor também funcionam
    int value = co_await dispatcher.dispatch([]() { return 42; });

    // Continua a corrotina na main thread
    co_await dispatcher.dispatch_main();
    do_something_in_main_thread();

    // Continua a corrotina num thread de background
    co_await dispatcher.dispatch();
    do_something_in_background();

    co_return 0;
}

// A corrotina começa imediatamente, mas suspende no primeiro `co_await`
task<int> t1 = my_coroutine();
```


## Próximos passos
Uma das poucas funcionalidades faltando que vi em algumas implementações é o suporte a cancelar tarefas despachadas.
Poderíamos usar as tags pra isso, de modo que tarefas com tags possam ser canceladas.
Adicionar suporte a tags em tarefas de *main loop* / tarefas com *delay* também seria útil nesse caso.

Além disso, fazer alguns *benchmarks* comparando com outras implementações (como [enkiTS](https://github.com/dougbinks/enkiTS) ou [BS::thread_pool](https://github.com/bshoshany/thread-pool)) seria uma adição bacana.


## Conclusão
Acho que é bem isso aí mesmo, até a próxima!
