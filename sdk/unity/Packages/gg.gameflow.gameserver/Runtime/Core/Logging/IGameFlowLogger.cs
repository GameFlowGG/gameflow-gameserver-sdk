namespace GameFlow
{
    /// <summary>Injectable, fully silenceable logger. Every implementation prefixes lines with <c>[gameflow]</c>.</summary>
    public interface IGameFlowLogger
    {
        void Info(string message);
        void Warn(string message);
        void Error(string message);
        void Debug(string message);
    }
}
